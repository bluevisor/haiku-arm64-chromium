// Exercises the virtio_gpu render node: params, Venus capset, context,
// a mapped host blob, and a fenced submit waited on through poll().
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>

#include "virtio_gpu_3d.h"


#define CHECK(expr) \
	do { \
		if ((expr) != 0) { \
			printf("FAIL %s: %s\n", #expr, strerror(errno)); \
			return 1; \
		} \
	} while (0)


int
main(int argc, char** argv)
{
	const char* path = argc > 1 ? argv[1] : VIRTIO_GPU_RENDER_DEVICE_DIR "/0";
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("FAIL open %s: %s\n", path, strerror(errno));
		return 1;
	}

	static const char* kParams[] = { "", "3d_features", "capset_query_fix",
		"resource_blob", "host_visible", "cross_device", "context_init",
		"supported_capset_ids", "explicit_debug_name", "blob_alignment" };
	for (uint64 i = 1; i <= 9; i++) {
		virtio_gpu_3d_getparam param = { i, 0 };
		if (ioctl(fd, VIRTIO_GPU_3D_GETPARAM, &param, sizeof(param)) != 0)
			printf("param %-22s error %s\n", kParams[i], strerror(errno));
		else
			printf("param %-22s %#llx\n", kParams[i], (unsigned long long)param.value);
	}

	uint32 capset[64] = {};
	virtio_gpu_3d_get_caps caps = {};
	caps.cap_set_id = VIRTIO_GPU_3D_CAPSET_VENUS;
	caps.addr = (addr_t)capset;
	caps.size = sizeof(capset);
	CHECK(ioctl(fd, VIRTIO_GPU_3D_GET_CAPS, &caps, sizeof(caps)));
	printf("venus capset: wire_format %u vk_xml %#x ext_serialization %u "
		"venus_protocol %u blob_id_0 %u\n", capset[0], capset[1], capset[2],
		capset[3], capset[4]);

	const char* name = "vgtest";
	virtio_gpu_3d_context_set_param params[] = {
		{ VIRTIO_GPU_3D_CONTEXT_PARAM_CAPSET_ID, VIRTIO_GPU_3D_CAPSET_VENUS },
		{ VIRTIO_GPU_3D_CONTEXT_PARAM_NUM_RINGS, 64 },
		{ VIRTIO_GPU_3D_CONTEXT_PARAM_POLL_RINGS_MASK, 0 },
		{ VIRTIO_GPU_3D_CONTEXT_PARAM_DEBUG_NAME, (addr_t)name },
	};
	virtio_gpu_3d_context_init init = { 4, 0, (addr_t)params };
	CHECK(ioctl(fd, VIRTIO_GPU_3D_CONTEXT_INIT, &init, sizeof(init)));
	printf("context: ok\n");

	// Venus asks for its shared memory this way: host3d, mappable, blob id 0.
	virtio_gpu_3d_resource_create_blob blob = {};
	blob.blob_mem = VIRTIO_GPU_3D_BLOB_MEM_HOST3D;
	blob.blob_flags = VIRTIO_GPU_3D_BLOB_FLAG_USE_MAPPABLE;
	blob.size = 64 * 1024;
	CHECK(ioctl(fd, VIRTIO_GPU_3D_RESOURCE_CREATE_BLOB, &blob, sizeof(blob)));
	printf("blob: handle %u resource %#x\n", blob.bo_handle, blob.res_handle);

	virtio_gpu_3d_resource_info info = { blob.bo_handle };
	CHECK(ioctl(fd, VIRTIO_GPU_3D_RESOURCE_INFO, &info, sizeof(info)));
	printf("info: resource %#x size %u blob_mem %u\n", info.res_handle,
		info.size, info.blob_mem);

	virtio_gpu_3d_map map = { blob.bo_handle };
	CHECK(ioctl(fd, VIRTIO_GPU_3D_MAP, &map, sizeof(map)));
	uint32* words = (uint32*)(addr_t)map.address;
	printf("map: area %d at %p\n", (int)map.area, words);

	bigtime_t start = system_time();
	for (uint32 i = 0; i < blob.size / 4; i++)
		words[i] = i * 2654435761u;
	uint32 bad = 0;
	for (uint32 i = 0; i < blob.size / 4; i++) {
		if (words[i] != i * 2654435761u)
			bad++;
	}
	// unaligned access, which device memory would fault on
	char* bytes = (char*)words;
	memcpy(bytes + 3, "unaligned", 9);
	printf("map: wrote+verified %llu KiB in %lld us, %u mismatches, "
		"unaligned ok\n", (unsigned long long)blob.size / 1024,
		(long long)(system_time() - start), bad);

	virtio_gpu_3d_handle handle = { blob.bo_handle };
	CHECK(ioctl(fd, VIRTIO_GPU_3D_UNMAP, &handle, sizeof(handle)));
	CHECK(ioctl(fd, VIRTIO_GPU_3D_CLOSE_HANDLE, &handle, sizeof(handle)));
	printf("blob: closed\n");

	// An empty submit, fenced on ring 0.
	virtio_gpu_3d_execbuffer exec = {};
	exec.flags = VIRTIO_GPU_3D_EXECBUF_FENCE | VIRTIO_GPU_3D_EXECBUF_RING_IDX;
	exec.ring_idx = 0;
	start = system_time();
	CHECK(ioctl(fd, VIRTIO_GPU_3D_EXECBUFFER, &exec, sizeof(exec)));
	printf("execbuffer: fence %llu\n", (unsigned long long)exec.fence_id);

	int fenceFd = open(path, O_RDWR);
	if (fenceFd < 0) {
		printf("FAIL open fence fd: %s\n", strerror(errno));
		return 1;
	}
	virtio_gpu_3d_fence fence = { exec.fence_id, 0 };
	CHECK(ioctl(fenceFd, VIRTIO_GPU_3D_FENCE_BIND, &fence, sizeof(fence)));
	struct pollfd pfd = { fenceFd, POLLIN, 0 };
	int ready = poll(&pfd, 1, 5000);
	printf("fence: poll returned %d revents %#x after %lld us\n", ready,
		pfd.revents, (long long)(system_time() - start));

	fence.timeout = 1000000;
	CHECK(ioctl(fd, VIRTIO_GPU_3D_FENCE_WAIT, &fence, sizeof(fence)));
	printf("fence: wait ok\n");

	close(fenceFd);
	close(fd);
	printf("PASS\n");
	return 0;
}
