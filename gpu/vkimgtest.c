// Prints memory requirements of the image kinds ANGLE creates.
#include <dlfcn.h>
#include <stdio.h>
#include <vulkan/vulkan.h>

static PFN_vkGetInstanceProcAddr gipa;

static void
probe(VkDevice device, PFN_vkCreateImage createImage,
	PFN_vkGetImageMemoryRequirements requirements, const char* name,
	VkSampleCountFlagBits samples, VkImageUsageFlags usage)
{
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.extent = (VkExtent3D){ 256, 256, 1 };
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = samples;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usage;
	VkImage image;
	VkResult result = createImage(device, &info, NULL, &image);
	if (result != VK_SUCCESS) {
		printf("%-28s create failed %d\n", name, result);
		return;
	}
	VkMemoryRequirements req;
	requirements(device, image, &req);
	printf("%-28s size %8llu align %6llu memoryTypeBits %#x\n", name,
		(unsigned long long)req.size, (unsigned long long)req.alignment,
		req.memoryTypeBits);
}

int
main(void)
{
	void* lib = dlopen("libvulkan.so.1", RTLD_NOW);
	gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
	PFN_vkCreateInstance createInstance
		= (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.pApplicationInfo = &app;
	VkInstance instance;
	createInstance(&ici, NULL, &instance);
	PFN_vkEnumeratePhysicalDevices enumerate
		= (PFN_vkEnumeratePhysicalDevices)gipa(instance, "vkEnumeratePhysicalDevices");
	PFN_vkCreateDevice createDevice
		= (PFN_vkCreateDevice)gipa(instance, "vkCreateDevice");
	PFN_vkGetDeviceProcAddr gdpa
		= (PFN_vkGetDeviceProcAddr)gipa(instance, "vkGetDeviceProcAddr");
	PFN_vkGetPhysicalDeviceFeatures getFeatures
		= (PFN_vkGetPhysicalDeviceFeatures)gipa(instance, "vkGetPhysicalDeviceFeatures");
	uint32_t count = 1;
	VkPhysicalDevice physical;
	enumerate(instance, &count, &physical);

	VkPhysicalDeviceFeatures f;
	getFeatures(physical, &f);
	printf("features: independentBlend %u geometryShader %u sampleRateShading %u "
		"dualSrcBlend %u logicOp %u depthClamp %u fillModeNonSolid %u "
		"wideLines %u largePoints %u vertexPipelineStoresAndAtomics %u "
		"fragmentStoresAndAtomics %u shaderClipDistance %u "
		"textureCompressionETC2 %u textureCompressionBC %u "
		"occlusionQueryPrecise %u pipelineStatisticsQuery %u "
		"robustBufferAccess %u fullDrawIndexUint32 %u imageCubeArray %u\n",
		f.independentBlend, f.geometryShader, f.sampleRateShading,
		f.dualSrcBlend, f.logicOp, f.depthClamp, f.fillModeNonSolid,
		f.wideLines, f.largePoints, f.vertexPipelineStoresAndAtomics,
		f.fragmentStoresAndAtomics, f.shaderClipDistance,
		f.textureCompressionETC2, f.textureCompressionBC,
		f.occlusionQueryPrecise, f.pipelineStatisticsQuery,
		f.robustBufferAccess, f.fullDrawIndexUint32, f.imageCubeArray);

	float priority = 1;
	VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	qi.queueCount = 1;
	qi.pQueuePriorities = &priority;
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qi;
	VkDevice device;
	if (createDevice(physical, &dci, NULL, &device) != VK_SUCCESS) {
		printf("FAIL device\n");
		return 1;
	}
	PFN_vkCreateImage createImage = (PFN_vkCreateImage)gdpa(device, "vkCreateImage");
	PFN_vkGetImageMemoryRequirements requirements
		= (PFN_vkGetImageMemoryRequirements)gdpa(device, "vkGetImageMemoryRequirements");

	const VkImageUsageFlags color = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		| VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		| VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	probe(device, createImage, requirements, "texture", VK_SAMPLE_COUNT_1_BIT, color);
	probe(device, createImage, requirements, "msaa x4", VK_SAMPLE_COUNT_4_BIT,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	probe(device, createImage, requirements, "msaa x4 transient",
		VK_SAMPLE_COUNT_4_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
	probe(device, createImage, requirements, "x1 transient",
		VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
	return 0;
}
