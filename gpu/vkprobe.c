// Loads Mesa's Venus ICD directly (Haiku has no Vulkan loader), prints the
// device, and runs a buffer copy + fill on the GPU, checked from the CPU.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vulkan/vulkan.h>

typedef VkResult (VKAPI_PTR *PFN_negotiate)(uint32_t*);

static PFN_vkGetInstanceProcAddr gipa;

#define CHECK(expr) \
	do { \
		VkResult _r = (expr); \
		if (_r != VK_SUCCESS) { \
			printf("FAIL %s: %d\n", #expr, _r); \
			return 1; \
		} \
	} while (0)

#define LOAD(instance, name) PFN_##name name = (PFN_##name)gipa(instance, #name)


static double
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}


int
main(int argc, char** argv)
{
	const char* path = argc > 1 ? argv[1] : "libvulkan_virtio.so";
	void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (lib == NULL) {
		printf("FAIL dlopen %s: %s\n", path, dlerror());
		return 1;
	}

	PFN_negotiate negotiate = (PFN_negotiate)dlsym(lib,
		"vk_icdNegotiateLoaderICDInterfaceVersion");
	gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
	if (negotiate == NULL || gipa == NULL) {
		printf("FAIL: not an ICD\n");
		return 1;
	}
	uint32_t icdVersion = 5;
	negotiate(&icdVersion);
	printf("icd interface %u\n", icdVersion);

	double t0 = now_ms();
	LOAD(NULL, vkCreateInstance);
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.pApplicationName = "vkprobe";
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instanceInfo.pApplicationInfo = &app;
	VkInstance instance;
	CHECK(vkCreateInstance(&instanceInfo, NULL, &instance));
	printf("instance: %.1f ms\n", now_ms() - t0);

	LOAD(instance, vkEnumeratePhysicalDevices);
	LOAD(instance, vkGetPhysicalDeviceProperties);
	LOAD(instance, vkGetPhysicalDeviceMemoryProperties);
	LOAD(instance, vkGetPhysicalDeviceQueueFamilyProperties);
	LOAD(instance, vkCreateDevice);
	LOAD(instance, vkGetDeviceProcAddr);
	LOAD(instance, vkDestroyInstance);

	uint32_t count = 0;
	CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
	if (count == 0) {
		printf("FAIL: no physical devices\n");
		return 1;
	}
	VkPhysicalDevice physical;
	count = 1;
	vkEnumeratePhysicalDevices(instance, &count, &physical);

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physical, &props);
	printf("device: %s\n  api %u.%u.%u, driver %#x, vendor %#x, device %#x, "
		"type %d\n", props.deviceName, VK_API_VERSION_MAJOR(props.apiVersion),
		VK_API_VERSION_MINOR(props.apiVersion),
		VK_API_VERSION_PATCH(props.apiVersion), props.driverVersion,
		props.vendorID, props.deviceID, props.deviceType);

	VkPhysicalDeviceMemoryProperties memory;
	vkGetPhysicalDeviceMemoryProperties(physical, &memory);
	uint32_t hostType = UINT32_MAX;
	for (uint32_t i = 0; i < memory.memoryTypeCount; i++) {
		VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
		printf("  memory type %u: heap %u flags %#x\n", i,
			memory.memoryTypes[i].heapIndex, flags);
		const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
			| VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		if (hostType == UINT32_MAX && (flags & want) == want)
			hostType = i;
	}
	for (uint32_t i = 0; i < memory.memoryHeapCount; i++) {
		printf("  heap %u: %llu MiB flags %#x\n", i,
			(unsigned long long)memory.memoryHeaps[i].size >> 20,
			memory.memoryHeaps[i].flags);
	}

	uint32_t familyCount = 8;
	VkQueueFamilyProperties families[8];
	vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families);
	uint32_t family = UINT32_MAX;
	for (uint32_t i = 0; i < familyCount; i++) {
		if (family == UINT32_MAX
			&& (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0)
			family = i;
	}

	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = family;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	VkDevice device;
	t0 = now_ms();
	CHECK(vkCreateDevice(physical, &deviceInfo, NULL, &device));
	printf("device created: %.1f ms\n", now_ms() - t0);

	PFN_vkGetDeviceProcAddr gdpa = vkGetDeviceProcAddr;
#define DLOAD(name) PFN_##name name = (PFN_##name)gdpa(device, #name)
	DLOAD(vkGetDeviceQueue);
	DLOAD(vkCreateBuffer);
	DLOAD(vkGetBufferMemoryRequirements);
	DLOAD(vkAllocateMemory);
	DLOAD(vkBindBufferMemory);
	DLOAD(vkMapMemory);
	DLOAD(vkCreateCommandPool);
	DLOAD(vkAllocateCommandBuffers);
	DLOAD(vkBeginCommandBuffer);
	DLOAD(vkCmdCopyBuffer);
	DLOAD(vkCmdFillBuffer);
	DLOAD(vkCmdPipelineBarrier);
	DLOAD(vkEndCommandBuffer);
	DLOAD(vkCreateFence);
	DLOAD(vkQueueSubmit);
	DLOAD(vkWaitForFences);
	DLOAD(vkResetFences);
	DLOAD(vkDeviceWaitIdle);
	DLOAD(vkDestroyDevice);

	VkQueue queue;
	vkGetDeviceQueue(device, family, 0, &queue);

	const VkDeviceSize size = 16 << 20;
	VkBuffer buffers[2];
	VkDeviceMemory memories[2];
	uint32_t* maps[2];
	for (int i = 0; i < 2; i++) {
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = size;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
			| VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		CHECK(vkCreateBuffer(device, &bufferInfo, NULL, &buffers[i]));
		VkMemoryRequirements requirements;
		vkGetBufferMemoryRequirements(device, buffers[i], &requirements);
		VkMemoryAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocInfo.allocationSize = requirements.size;
		allocInfo.memoryTypeIndex = hostType;
		CHECK(vkAllocateMemory(device, &allocInfo, NULL, &memories[i]));
		CHECK(vkBindBufferMemory(device, buffers[i], memories[i], 0));
		CHECK(vkMapMemory(device, memories[i], 0, size, 0, (void**)&maps[i]));
	}
	printf("buffers: 2 x %llu MiB host-visible, mapped at %p and %p\n",
		(unsigned long long)size >> 20, maps[0], maps[1]);

	const uint32_t words = size / 4;
	for (uint32_t i = 0; i < words; i++)
		maps[0][i] = i * 2654435761u;
	memset(maps[1], 0, size);

	VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolInfo.queueFamilyIndex = family;
	VkCommandPool pool;
	CHECK(vkCreateCommandPool(device, &poolInfo, NULL, &pool));
	VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmdInfo.commandPool = pool;
	cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdInfo.commandBufferCount = 1;
	VkCommandBuffer cmd;
	CHECK(vkAllocateCommandBuffers(device, &cmdInfo, &cmd));

	// copy the first half, fill the second half with 0xc0ffee
	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CHECK(vkBeginCommandBuffer(cmd, &begin));
	VkBufferCopy region = { 0, 0, size / 2 };
	vkCmdCopyBuffer(cmd, buffers[0], buffers[1], 1, &region);
	vkCmdFillBuffer(cmd, buffers[1], size / 2, size / 2, 0xc0ffee);
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
	CHECK(vkEndCommandBuffer(cmd));

	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence;
	CHECK(vkCreateFence(device, &fenceInfo, NULL, &fence));

	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	double best = 1e9;
	for (int round = 0; round < 5; round++) {
		CHECK(vkResetFences(device, 1, &fence));
		t0 = now_ms();
		CHECK(vkQueueSubmit(queue, 1, &submit, fence));
		CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
		double ms = now_ms() - t0;
		if (ms < best)
			best = ms;
	}
	printf("submit+wait (8 MiB copy + 8 MiB fill): best of 5 %.2f ms\n", best);

	uint32_t bad = 0;
	for (uint32_t i = 0; i < words / 2; i++) {
		if (maps[1][i] != i * 2654435761u)
			bad++;
	}
	for (uint32_t i = words / 2; i < words; i++) {
		if (maps[1][i] != 0xc0ffee)
			bad++;
	}
	printf("verify: %u mismatches of %u words\n", bad, words);

	vkDeviceWaitIdle(device);
	vkDestroyDevice(device, NULL);
	vkDestroyInstance(instance, NULL);
	printf(bad == 0 ? "PASS\n" : "FAIL\n");
	return bad != 0;
}
