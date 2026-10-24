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
| 3 | Chromium WebGL through ANGLE's Vulkan backend on Venus | done: WebGL 2 on the host GPU (ANGLE fix + KosmicKrisp fix) |
| 4 | GPU compositing and rasterization | done: readback view surface, on by default |

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

## Phase 3: Chromium WebGL on Venus

`venus-shot.sh` runs a throwaway Chromium with `~/venus-lib`
(`libvulkan.so.1` + `libvulkan_virtio.so`) first on `LIBRARY_PATH` and
`--use-angle=vulkan`. ANGLE comes up on Venus. Three things stood between
that and a working WebGL 2 context; the first two are small, the third took
a debugger on the host.

### ANGLE: implicit MSAA needs a lazily allocated memory type

WebGL contexts failed with "Internal Vulkan error (-8)" in
`initMemoryAndNonZeroFillIfNeeded`. Without the MSAA render-to-texture
extension ANGLE emulates it with a transient 4x image, and when the device
has a lazily allocated memory type it *requires* that type, although the
image's `memoryTypeBits` may not include it (MoltenVK: `0x3` for ANGLE's
mutable-format, input-attachment color image). `-8` is VMA finding no memory
type; `-9` is the same failure in ANGLE's own path. The fix
(`angle-msrtt-lazy-memory.diff`, also in `patches/third_party__angle.diff`)
drops the requirement when the image's types don't include it. Spec-correct,
nothing to hide in Venus.

### MoltenVK caps ANGLE at ES 2.0

ANGLE's Vulkan backend needs `VK_EXT_provoking_vertex` for ES 3.0, and
MoltenVK 1.4.2 doesn't have it (`vkcaps.c` on the guest, `hostvk.c` on the
host). UTM 5 ships a second Vulkan driver, Mesa's KosmicKrisp, which does
(Vulkan 1.4.343, `provokingVertexLast`). It is a global UTM preference:

```sh
defaults write com.utmapp.UTM QEMUVulkanDriver -int 3   # KosmicKrisp; delete the key for MoltenVK
```

(power-cycle the VM; `ps -wwE` on `QEMURenderServer` shows `VK_DRIVER_FILES`
pointing at `kosmickrisp_mesa_icd.json` when it took).

### KosmicKrisp: optimal-tiled images on host-pointer memory are nil textures

With KosmicKrisp every image workload died: `vkclear.c` hung the guest and
ANGLE's second submit got `VK_ERROR_DEVICE_LOST`. The host said only
`vkQueueSubmit resulted in CS error`. What actually happens, pieced together
from the worker crash reports (`~/Library/Logs/DiagnosticReports/
QEMURenderServer-*.ips`: `copyFromTexture:toBuffer:` faulting at `0x220`, a
nil `MTLTexture`), the guest probes (`LINEAR=1 vkclear` passes, optimal
tiling dies even with the readback buffer never mapped), and finally lldb on
the render worker (`mtl_new_texture_with_descriptor(heap = NULL, …)` from
`kk_BindImageMemory2`; `kk_AllocateMemory`'s `pNext` is
`VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT`):

1. UTM's virglrenderer (`macos-next`) has no cross-process Vulkan handle on
   macOS, so for every allocation of a host-visible memory type it allocates
   an anonymous shared-memory file and imports it with
   `VK_EXT_external_memory_host` (`force_shm_import` in
   `vkr_device_memory.c`). That is how QEMU maps guest-visible blobs.
2. KosmicKrisp exposes exactly one memory type, `0xf` (device-local *and*
   host-visible). So every allocation, images included, is a host-pointer
   import. MoltenVK is unaffected only because its type 0 (`0x1`) isn't
   host-visible, so images never take that path.
3. KosmicKrisp backs an imported host pointer with an `MTLBuffer` only
   ("the imported memory will only be usable for buffers") and leaves the
   `MTLHeap` NULL. Linear images are created from the buffer and work;
   optimal images are placed in the heap, so `[nil newTextureWithDescriptor:
   offset:]` returns nil. A release build has no check: the copy segfaults,
   a render pass makes Metal fail the command buffer and the device is lost.

`hostclear.c` reproduces it natively on the Mac without Venus:
`hostclear import` (image bound to imported host memory) crashes with the
same stack; `hostclear export` (memory exportable as an MTLHeap, what the
`macos` branch of UTM's virglrenderer does) passes, which is why reading
that branch first was a dead end.

The guest can't work around it (the host decides per memory-type flags), so
the fix is in KosmicKrisp: `kosmickrisp-host-pointer-images.diff` (against
UTM's pinned Mesa, `osy/mesa` eed3e1d9bf). When an optimal-tiled image is
bound to memory that has no heap, the plane gets a standalone device texture
in the device's residency set instead of a placement in the heap. That is
legal: the contents of optimal-tiled image memory are opaque, nothing can
observe that the texture isn't in the memory. What it loses is aliasing: a
3D image's 2D-array alias and the compressed-to-uncompressed subresource
view are separate allocations on such planes (left as TODOs).

Build (macOS, Homebrew `meson llvm bison flex spirv-llvm-translator`,
`pip install mako pyyaml packaging`):

```sh
PATH="$(brew --prefix llvm)/bin:$(brew --prefix bison)/bin:$(brew --prefix flex)/bin:$PATH" \
PKG_CONFIG_PATH="$(brew --prefix spirv-llvm-translator)/lib/pkgconfig" \
meson setup build --buildtype=release -Dllvm=enabled -Dopengl=false -Dgallium-drivers= \
  -Dvulkan-drivers=kosmickrisp -Dplatforms=macos -Dmesa-clc=enabled -Dprecomp-compiler=enabled
meson compile -C build    # build/src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib
```

The dylib links Homebrew's `libzstd` and `libSPIRV-Tools`; point the first at
UTM's `zstd.1.framework` and bundle the second next to the driver
(`install_name_tool`), or the sandboxed render server can't load it (ANGLE
then reports `-9` at `vkCreateInstance`).

Stock UTM is hardened and sandboxed, so this needs a re-signed copy of
UTM.app: ad-hoc signed with UTM's own `*-unsigned.entitlements` (keep the
team-prefixed app group or SPICE can't connect; vmnet networking needs
Apple's restricted entitlement, so switch the VM to emulated networking with
a port forward for ssh), `get-task-allow` on the render server for lldb, and
library validation off so the rebuilt driver loads.

### Result

With the three fixes, Chromium reports
`WebGL 2.0 (OpenGL ES 3.0 Chromium)` on
`ANGLE (Apple, Vulkan 1.4.343 (Virtio-GPU Venus (Apple M5 Pro)), venus)`,
`webgl2-probe.html` reads back the drawn triangle correctly, and the WebGL
Aquarium runs 500 fish at 25 fps in the 1024x1024 canvas with no GPU errors
(`webgl2-venus-kosmickrisp.png`, `webgl-aquarium-venus-kosmickrisp.png`).
With MoltenVK (no KosmicKrisp patch needed) the same path gives WebGL 1 only.

The launcher (`vm/HaikuChrome`) now runs ANGLE on Venus by default, with
SwiftShader kept as a fallback (`HAIKUCHROME_SWIFTSHADER=1`). Still to do:
get the KosmicKrisp fix upstream so stock UTM works.

## Phase 4: GPU compositing

With Phase 3 only WebGL ran on the GPU; viz composited in software and
rasterized every tile on the CPU. app_server has no GL or Vulkan surface to
hand out, so the display path is the one the X11 backend uses without GLX
(`GLSurfaceEglReadbackX11`): viz renders each frame into an EGL pbuffer on
ANGLE, `SwapBuffers()` reads the pixels back (`glReadPixels`, BGRA), and
they go to the BWindow the way the software compositor's bitmaps did.

- `GLSurfaceEglReadbackHaiku` (`ui/ozone/platform/haiku/haiku_surface_factory.cc`)
  subclasses `ui::GLSurfaceEglReadback`; `HandlePixels()` copies the rows
  into a `BBitmap` and hands it to the window's `HaikuPresentTarget`, the
  same thread-safe handle the canvas surface uses. `GLOzoneEGLHaiku::
  CreateViewGLSurface()` builds one per widget from the window manager.
- `gpu/ipc/service/image_transport_surface_haiku.cc` used to return null
  from `CreateNativeGLSurface()`. viz then failed to create its output
  surface and sent `DisableGpuCompositing()` to the browser before Ozone was
  ever asked, while blink kept producing GPU resources: every renderer frame
  was dropped with `resolved_frame_data.cc Invalid resource for SurfaceId`
  and WebGL contexts were lost. It now calls
  `gl::init::CreateViewGLSurface()` like Linux. (Finding that took a
  `--v=1` run: the only trace is `viz_process_transport_factory.cc
  "Switching to software compositing"`.)
- The launcher runs with GPU compositing by default;
  `HAIKUCHROME_SOFTWARE_COMPOSITING=1` restores `--disable-gpu-compositing`.

Measured: WebGL Aquarium 500 fish at 22 fps with the whole 1800x1090 window
read back per frame (25 fps when only the canvas was read back under software
compositing), no GPU errors. The readback is one `glReadPixels` through
Venus plus one `memcpy` into the BBitmap per frame; a mapped-blob readback
or a damage-rect `PostSubBuffer` would cut it.
