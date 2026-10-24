// vkclear.c run natively on the Mac against a host driver (through UTM's
// loader, pick the driver with VK_DRIVER_FILES), with every step logged. Tells
// whether a render-pass clear + copy fails in the host driver alone or only
// through Venus/vkr.
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define CHECK(expr) do { printf("  %s\n", #expr); VkResult _r = (expr); \
	if (_r != VK_SUCCESS) { printf("FAIL %s: %d\n", #expr, _r); return 1; } } while (0)

int
main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	// "import": back the image memory with an imported host pointer
	// (VK_EXT_external_memory_host), the way vkr backs guest-mappable blobs.
	int importHost = argc > 1 && strcmp(argv[1], "import") == 0;
	// "export": allocate the image memory exportable as a Metal heap
	// (VK_EXT_external_memory_metal), the way UTM's vkr allocates host-visible memory.
	int exportMetal = argc > 1 && strcmp(argv[1], "export") == 0;
	// "fork": initialise the driver (and Metal) once, fork(), and run the
	// whole test in the child, the way a render-server worker runs.
	int forkFirst = argc > 1 && strcmp(argv[1], "fork") == 0;
	void* lib = dlopen("/Applications/UTM.app/Contents/Frameworks/vulkan.1.framework/vulkan.1",
		RTLD_NOW);
	if (lib == NULL) {
		printf("FAIL dlopen: %s\n", dlerror());
		return 1;
	}
	PFN_vkGetInstanceProcAddr gipa
		= (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
	PFN_vkCreateInstance vkCreateInstance
		= (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
	const char* instanceExts[] = { "VK_KHR_portability_enumeration",
		"VK_KHR_get_physical_device_properties2" };
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	ici.pApplicationInfo = &app;
	ici.enabledExtensionCount = 2;
	ici.ppEnabledExtensionNames = instanceExts;
	VkInstance instance;
	if (forkFirst) {
		VkInstance warm;
		CHECK(vkCreateInstance(&ici, NULL, &warm));
		PFN_vkEnumeratePhysicalDevices enumerate
			= (PFN_vkEnumeratePhysicalDevices)gipa(warm, "vkEnumeratePhysicalDevices");
		PFN_vkCreateDevice createDevice = (PFN_vkCreateDevice)gipa(warm, "vkCreateDevice");
		uint32_t n = 1;
		VkPhysicalDevice pd;
		enumerate(warm, &n, &pd);
		float prio = 1;
		VkDeviceQueueCreateInfo wq = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		wq.queueCount = 1;
		wq.pQueuePriorities = &prio;
		VkDeviceCreateInfo wd = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		wd.queueCreateInfoCount = 1;
		wd.pQueueCreateInfos = &wq;
		VkDevice warmDev;
		CHECK(createDevice(pd, &wd, NULL, &warmDev));
		pid_t child = fork();
		if (child != 0) {
			int status = 0;
			waitpid(child, &status, 0);
			printf("child exited with status %#x\n", status);
			return 0;
		}
		printf("in forked child %d\n", getpid());
	}
	CHECK(vkCreateInstance(&ici, NULL, &instance));
#define I(n) PFN_##n n = (PFN_##n)gipa(instance, #n)
	I(vkEnumeratePhysicalDevices); I(vkCreateDevice); I(vkGetDeviceProcAddr);
	I(vkGetPhysicalDeviceMemoryProperties); I(vkGetPhysicalDeviceProperties);
	I(vkEnumerateDeviceExtensionProperties);
	uint32_t count = 1;
	VkPhysicalDevice physical;
	vkEnumeratePhysicalDevices(instance, &count, &physical);
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physical, &props);
	printf("device %s api %u.%u.%u\n", props.deviceName,
		VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
		VK_API_VERSION_PATCH(props.apiVersion));
	VkPhysicalDeviceMemoryProperties mem;
	vkGetPhysicalDeviceMemoryProperties(physical, &mem);
	for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
		printf("memory type %u: flags %#x heap %u\n", i, mem.memoryTypes[i].propertyFlags,
			mem.memoryTypes[i].heapIndex);

	VkExtensionProperties exts[512];
	count = 512;
	vkEnumerateDeviceExtensionProperties(physical, NULL, &count, exts);
	const char* deviceExts[4];
	uint32_t deviceExtCount = 0;
	for (uint32_t i = 0; i < count; i++)
		if (strcmp(exts[i].extensionName, "VK_KHR_portability_subset") == 0)
			deviceExts[deviceExtCount++] = "VK_KHR_portability_subset";
	if (importHost)
		deviceExts[deviceExtCount++] = "VK_EXT_external_memory_host";
	if (exportMetal) {
		deviceExts[deviceExtCount++] = "VK_KHR_external_memory";
		deviceExts[deviceExtCount++] = "VK_EXT_external_memory_metal";
	}
	float priority = 1;
	VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	qi.queueCount = 1;
	qi.pQueuePriorities = &priority;
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qi;
	dci.enabledExtensionCount = deviceExtCount;
	dci.ppEnabledExtensionNames = deviceExts;
	VkDevice dev;
	CHECK(vkCreateDevice(physical, &dci, NULL, &dev));
#define D(n) PFN_##n n = (PFN_##n)vkGetDeviceProcAddr(dev, #n)
	D(vkGetDeviceQueue); D(vkCreateImage); D(vkGetImageMemoryRequirements);
	D(vkAllocateMemory); D(vkBindImageMemory); D(vkCreateImageView);
	D(vkCreateRenderPass); D(vkCreateFramebuffer); D(vkCreateBuffer);
	D(vkGetBufferMemoryRequirements); D(vkBindBufferMemory); D(vkMapMemory);
	D(vkCreateCommandPool); D(vkAllocateCommandBuffers); D(vkBeginCommandBuffer);
	D(vkCmdBeginRenderPass); D(vkCmdEndRenderPass); D(vkCmdCopyImageToBuffer);
	D(vkEndCommandBuffer); D(vkQueueSubmit); D(vkQueueWaitIdle);
	VkQueue queue;
	vkGetDeviceQueue(dev, 0, 0, &queue);

	VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ii.imageType = VK_IMAGE_TYPE_2D;
	ii.format = VK_FORMAT_R8G8B8A8_UNORM;
	ii.extent = (VkExtent3D){ 256, 256, 1 };
	ii.mipLevels = ii.arrayLayers = 1;
	ii.samples = VK_SAMPLE_COUNT_1_BIT;
	ii.tiling = VK_IMAGE_TILING_OPTIMAL;
	ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	VkImage image;
	CHECK(vkCreateImage(dev, &ii, NULL, &image));
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(dev, image, &req);
	uint32_t type = 0;
	while (!(req.memoryTypeBits & (1u << type))) type++;
	printf("image: bits %#x size %llu align %llu, using type %u flags %#x\n",
		req.memoryTypeBits, (unsigned long long)req.size,
		(unsigned long long)req.alignment, type, mem.memoryTypes[type].propertyFlags);
	VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = type;
	VkImportMemoryHostPointerInfoEXT hostInfo
		= { VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT };
	if (importHost) {
		size_t size = (req.size + 16383) & ~(size_t)16383;
		void* host;
		posix_memalign(&host, 16384, size);
		memset(host, 0, size);
		hostInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
		hostInfo.pHostPointer = host;
		ai.pNext = &hostInfo;
		ai.allocationSize = size;
		printf("image memory: importing host pointer %p (%zu bytes)\n", host, size);
	}
	VkExportMemoryAllocateInfo exportInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
	if (exportMetal) {
		exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
		ai.pNext = &exportInfo;
		ai.allocationSize = (req.size + 4095) & ~(VkDeviceSize)4095;
		printf("image memory: exportable as MTLHeap (%llu bytes)\n",
			(unsigned long long)ai.allocationSize);
	}
	VkDeviceMemory imageMemory;
	CHECK(vkAllocateMemory(dev, &ai, NULL, &imageMemory));
	ai.pNext = NULL;
	CHECK(vkBindImageMemory(dev, image, imageMemory, 0));
	VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	vi.image = image;
	vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vi.format = ii.format;
	vi.subresourceRange = (VkImageSubresourceRange){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageView view;
	CHECK(vkCreateImageView(dev, &vi, NULL, &view));

	VkAttachmentDescription att = { 0 };
	att.format = ii.format;
	att.samples = VK_SAMPLE_COUNT_1_BIT;
	att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkSubpassDescription sub = { 0 };
	sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub.colorAttachmentCount = 1;
	sub.pColorAttachments = &ref;
	VkRenderPassCreateInfo rpi = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rpi.attachmentCount = 1;
	rpi.pAttachments = &att;
	rpi.subpassCount = 1;
	rpi.pSubpasses = &sub;
	VkRenderPass pass;
	CHECK(vkCreateRenderPass(dev, &rpi, NULL, &pass));
	VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fi.renderPass = pass;
	fi.attachmentCount = 1;
	fi.pAttachments = &view;
	fi.width = fi.height = 256;
	fi.layers = 1;
	VkFramebuffer fb;
	CHECK(vkCreateFramebuffer(dev, &fi, NULL, &fb));

	VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bi.size = 256 * 256 * 4;
	bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	VkBuffer buffer;
	CHECK(vkCreateBuffer(dev, &bi, NULL, &buffer));
	vkGetBufferMemoryRequirements(dev, buffer, &req);
	type = 0;
	while (!((req.memoryTypeBits & (1u << type))
		&& (mem.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) type++;
	printf("buffer: bits %#x size %llu, using type %u\n", req.memoryTypeBits,
		(unsigned long long)req.size, type);
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = type;
	VkDeviceMemory bufferMemory;
	CHECK(vkAllocateMemory(dev, &ai, NULL, &bufferMemory));
	CHECK(vkBindBufferMemory(dev, buffer, bufferMemory, 0));
	uint8_t* pixels;
	CHECK(vkMapMemory(dev, bufferMemory, 0, bi.size, 0, (void**)&pixels));
	memset(pixels, 0, bi.size);

	VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	VkCommandPool pool;
	CHECK(vkCreateCommandPool(dev, &pi, NULL, &pool));
	VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cai.commandPool = pool;
	cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cai.commandBufferCount = 1;
	VkCommandBuffer cmd;
	CHECK(vkAllocateCommandBuffers(dev, &cai, &cmd));
	VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CHECK(vkBeginCommandBuffer(cmd, &cbi));
	VkClearValue clear = { .color = { .float32 = { 1.0f, 0.5f, 0.0f, 1.0f } } };
	VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rbi.renderPass = pass;
	rbi.framebuffer = fb;
	rbi.renderArea.extent = (VkExtent2D){ 256, 256 };
	rbi.clearValueCount = 1;
	rbi.pClearValues = &clear;
	vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdEndRenderPass(cmd);
	VkBufferImageCopy region = { 0 };
	region.imageSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	region.imageExtent = (VkExtent3D){ 256, 256, 1 };
	vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
	CHECK(vkEndCommandBuffer(cmd));
	VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;
	CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
	CHECK(vkQueueWaitIdle(queue));
	printf("pixel(128,128) = %u,%u,%u,%u (want 255,128,0,255)\n", pixels[(128 * 256 + 128) * 4],
		pixels[(128 * 256 + 128) * 4 + 1], pixels[(128 * 256 + 128) * 4 + 2],
		pixels[(128 * 256 + 128) * 4 + 3]);
	CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
	CHECK(vkQueueWaitIdle(queue));
	printf("second submit ok\nPASS\n");
	return 0;
}
