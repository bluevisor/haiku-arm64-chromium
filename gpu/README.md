# GPU acceleration (work in progress)

Goal: run Chromium's WebGL and compositing on the Mac's GPU from the Haiku
arm64 VM in UTM, through Venus (Vulkan forwarded over virtio-gpu).

UTM 5 already starts QEMU with
`-device virtio-gpu-gl-pci,hostmem=8G,blob=true,venus=true`, and its
virglrenderer is built with Venus. Everything missing is on the Haiku side.

| Phase | What | Status |
|---|---|---|
| 1 | Haiku `virtio_gpu` kernel driver: 3D contexts, host-visible blobs, fences | done |
| 2 | Mesa's Venus Vulkan driver built for Haiku | done |
| 3 | Chromium WebGL through ANGLE's Vulkan backend on Venus | ANGLE starts; one ANGLE fix needed |
| 4 | GPU compositing and rasterization | |

## Phase 1: kernel driver

`haiku-virtio_gpu-3d.diff` applies to Haiku at hrev60290
(`git apply` in the Haiku tree). It changes three files:

- `virtio_gpu.cpp` negotiates `VIRGL`, `RESOURCE_BLOB` and
  `CONTEXT_INIT` on top of `EDID`. It reads the capsets and finds the
  host-visible shared memory region (virtio PCI capability type 8; the virtio
  bus manager doesn't expose it, so the driver reads it from the PCI device).
  The control queue is now asynchronous: each request has its own buffer, and
  a completion thread, woken by the queue interrupt, hands out the responses.
- It publishes a render node, `/dev/misc/virtio_gpu/<n>`, next to the display
  device. The display device can't serve 3D clients because opening it sets
  up the framebuffer.
- `virtio_gpu_3d.h` (new, `headers/private/graphics/virtio`) is the render
  node's ioctl interface. It mirrors the Linux virtgpu DRM uAPI, so Mesa's
  backend ports with few changes. The differences are documented in the
  header: `MAP` maps the blob into the calling team and returns the address
  (Haiku has no `mmap()` on devices), and a fenced `EXECBUFFER` returns a
  fence id. A second open of the render node, bound to that id with
  `FENCE_BIND`, becomes readable through `select()`/`poll()` once the fence
  signals. That fd stands in for Linux's sync_file.
- `viogpu.h` gains the context-init fields (`ring_idx`, `context_init`) and
  capset ids.

Blob mappings use write-back memory. The default memory type for physical
mappings on arm64 is Device-nGnRnE, which faults on the unaligned accesses
`memcpy()` makes.

Install it as a user add-on, so that the stock driver stays in place and the
boot menu's "Disable user add-ons" falls back to it:

```sh
jam -q virtio_gpu        # in generated.arm64
scp objects/haiku/arm64/release/add-ons/kernel/drivers/graphics/virtio/virtio_gpu \
    haiku:config/non-packaged/add-ons/kernel/drivers/graphics/virtio_gpu
ssh haiku 'shutdown -r'
```

`vgtest.cpp` exercises the render node (`g++ -o vgtest vgtest.cpp` on the VM,
next to `virtio_gpu_3d.h`). On UTM 5.0.8 it reports the Venus capset (wire
format 1, protocol 4, Vulkan 1.4 headers), creates a Venus context, maps a
64 KiB host blob and verifies it (44 µs), and waits on a fenced submit through
`poll()` (177 µs).

## Phase 2: Mesa's Venus driver

`mesa-26.2.5-haiku-venus.diff` applies to Mesa 26.2.5. Mesa only builds
Venus's kernel backend where there is Linux-style DRM, so Haiku gets its own:

- `vn_renderer_haiku.c` (new) is `vn_renderer_virtgpu.c` moved onto the
  render node. Mapping goes through `MAP`/`UNMAP`. A fenced submit's fence id
  becomes an fd by opening the render node and binding it, which Venus's
  simulated syncobjs then `poll()` like a sync_file. There is no dma-buf on
  Haiku, so import and export of it are off.
- `meson.build` builds it plus `vn_renderer_sim_syncobj.c` on Haiku and links
  `libnetwork` (the vtest backend's sockets). `vn_renderer.h` lets Haiku use the
  virtgpu path without libdrm, and `vn_common.h` uses `find_thread(NULL)` for
  `gettid()`.

It builds natively on the VM in a couple of minutes:

```sh
pkgman install zlib_devel expat_devel
python3 -m pip install --user meson mako pyyaml packaging
meson setup build-venus -Dvulkan-drivers=virtio -Dgallium-drivers= -Dplatforms= \
  -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Degl=disabled -Dglx=disabled \
  -Dgbm=disabled -Dllvm=disabled -Dzstd=disabled -Dvalgrind=disabled \
  -Dlibunwind=disabled -Dbuild-tests=false -Dvideo-codecs= -Dvulkan-layers= \
  -Dtools= -Dbuildtype=release -Db_ndebug=true
ninja -C build-venus     # src/virtio/vulkan/libvulkan_virtio.so
```

Haiku has no Vulkan loader. `vulkan_shim.c` builds a `libvulkan.so.1` that
exposes the ICD's `vk_icdGetInstanceProcAddr` as `vkGetInstanceProcAddr`
(all ANGLE's volk needs) and reports no layers:

```sh
gcc -shared -fPIC -I<mesa>/include -Wl,-soname,libvulkan.so.1 \
    -o libvulkan.so.1 vulkan_shim.c -L. -lvulkan_virtio
```

Results on UTM 5.0.8, MacBook Pro M5 Pro (`vkprobe.c`):

- Device "Virtio-GPU Venus (Apple M5 Pro)", Vulkan 1.3.334 (host instance
  1.4.337), one 64 GiB device-local heap, memory types `0x1`, `0xf`
  (host visible, coherent, cached) and `0x11` (lazily allocated).
- Instance extensions include `VK_EXT_headless_surface`.
- An 8 MiB buffer copy plus an 8 MiB fill on the GPU, waited on with a
  VkFence: 0.63 ms, verified.

Venus logs at debug level, which release builds hide: set
`MESA_LOG_LEVEL=debug` together with `VN_DEBUG=init,result`.

## Phase 3 findings so far

`venus-shot.sh` runs a throwaway Chromium with `~/venus-lib`
(`libvulkan.so.1` + `libvulkan_virtio.so`) first on `LIBRARY_PATH` and
`--use-angle=vulkan`. ANGLE comes up on Venus: the WebGL renderer string is
"ANGLE (Apple, Vulkan 1.1.334 (Virtio-GPU Venus (Apple M5 Pro)), venus)".
Two problems remain:

- **WebGL contexts fail** with "Internal Vulkan error (-8)" in
  `initMemoryAndNonZeroFillIfNeeded`. Without the MSAA extension ANGLE
  emulates multisampled render-to-texture with a transient 4x image, and
  when the device has a lazily allocated memory type it requires that type.
  The host doesn't allow it for the color image ANGLE creates (mutable format,
  input attachment usage: `memoryTypeBits` `0x3`), which the spec permits.
  (`-8` is VMA finding no memory type; with
  `--disable-angle-features=useVmaForImageSuballocation` it becomes `-9`.)
  The fix is to drop the lazily-allocated requirement when the image's types
  don't include it.
- **WebGL 2 is unavailable**: ANGLE caps at ES 2.0 because the host has no
  `VK_EXT_provoking_vertex` (`vkcaps.c`). Everything else ES 3.0 needs is
  present.
