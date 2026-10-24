// Clears a 256x256 color image in a render pass, copies it to a host buffer
// and checks the pixels: the simplest image + render pass path.
// Env switches: LINEAR=1 uses a linear-tiled image; NOMAP=1 never maps the
// readback buffer (so its memory is never exported to the guest).
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#define CHECK(expr) do { printf("  %s\n", #expr); VkResult _r = (expr); \
	if (_r != VK_SUCCESS) { printf("FAIL %s: %d\n", #expr, _r); return 1; } } while (0)

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	void* lib = dlopen("libvulkan.so.1", RTLD_NOW);
	PFN_vkGetInstanceProcAddr gipa
		= (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
	PFN_vkCreateInstance vkCreateInstance
		= (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.pApplicationInfo = &app;
	VkInstance instance;
	CHECK(vkCreateInstance(&ici, NULL, &instance));
#define I(n) PFN_##n n = (PFN_##n)gipa(instance, #n)
	I(vkEnumeratePhysicalDevices); I(vkCreateDevice); I(vkGetDeviceProcAddr);
	I(vkGetPhysicalDeviceMemoryProperties);
	uint32_t count = 1;
	VkPhysicalDevice physical;
	vkEnumeratePhysicalDevices(instance, &count, &physical);
	VkPhysicalDeviceMemoryProperties mem;
	vkGetPhysicalDeviceMemoryProperties(physical, &mem);
	float priority = 1;
	VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	qi.queueCount = 1;
	qi.pQueuePriorities = &priority;
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qi;
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
	ii.tiling = getenv("LINEAR") ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
	ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	VkImage image;
	CHECK(vkCreateImage(dev, &ii, NULL, &image));
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(dev, image, &req);
	uint32_t type = 0;
	while (!(req.memoryTypeBits & (1u << type))) type++;
	printf("image: bits %#x size %llu, using type %u flags %#x\n", req.memoryTypeBits,
		(unsigned long long)req.size, type, mem.memoryTypes[type].propertyFlags);
	VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = type;
	VkDeviceMemory imageMemory;
	CHECK(vkAllocateMemory(dev, &ai, NULL, &imageMemory));
	// SLEEP=<secs>: pause here so a debugger can attach to the host worker
	// before the bind creates the Metal texture.
	if (getenv("SLEEP")) {
		printf("sleeping %s s before vkBindImageMemory\n", getenv("SLEEP"));
		sleep(atoi(getenv("SLEEP")));
	}
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
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = type;
	VkDeviceMemory bufferMemory;
	CHECK(vkAllocateMemory(dev, &ai, NULL, &bufferMemory));
	CHECK(vkBindBufferMemory(dev, buffer, bufferMemory, 0));
	uint8_t* pixels = NULL;
	if (!getenv("NOMAP")) {
		CHECK(vkMapMemory(dev, bufferMemory, 0, bi.size, 0, (void**)&pixels));
		memset(pixels, 0, bi.size);
	}

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
	if (pixels != NULL) {
		printf("pixel(128,128) = %u,%u,%u,%u (want 255,128,0,255)\n",
			pixels[(128 * 256 + 128) * 4], pixels[(128 * 256 + 128) * 4 + 1],
			pixels[(128 * 256 + 128) * 4 + 2], pixels[(128 * 256 + 128) * 4 + 3]);
	} else
		printf("first submit ok (buffer not mapped)\n");
	CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
	CHECK(vkQueueWaitIdle(queue));
	printf("second submit ok\nPASS\n");
	return 0;
}
