# GPU acceleration (work in progress)

Goal: run Chromium's WebGL and compositing on the Mac's GPU from the Haiku
arm64 VM in UTM, through Venus (Vulkan forwarded over virtio-gpu).

UTM 5 already starts QEMU with
`-device virtio-gpu-gl-pci,hostmem=8G,blob=true,venus=true`, and its
virglrenderer is built with Venus. Everything missing is on the Haiku side.

| Phase | What | Status |
|---|---|---|
| 1 | Haiku `virtio_gpu` kernel driver: 3D contexts, host-visible blobs, fences | done |
| 2 | Mesa's Venus Vulkan driver cross-built for Haiku | in progress |
| 3 | Chromium WebGL through ANGLE's Vulkan backend on Venus | |
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
