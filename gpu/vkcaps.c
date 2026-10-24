// Prints the Vulkan capabilities that decide ANGLE's maximum ES version.
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

int
main(void)
{
	void* lib = dlopen("libvulkan.so.1", RTLD_NOW);
	PFN_vkGetInstanceProcAddr gipa
		= (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
	PFN_vkCreateInstance createInstance
		= (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.pApplicationInfo = &app;
	VkInstance instance;
	createInstance(&ici, NULL, &instance);
#define I(name) PFN_##name name = (PFN_##name)gipa(instance, #name)
	I(vkEnumeratePhysicalDevices);
	I(vkEnumerateDeviceExtensionProperties);
	I(vkGetPhysicalDeviceProperties);
	I(vkGetPhysicalDeviceFeatures2);
	uint32_t count = 1;
	VkPhysicalDevice physical;
	vkEnumeratePhysicalDevices(instance, &count, &physical);

	VkExtensionProperties exts[512];
	count = 512;
	vkEnumerateDeviceExtensionProperties(physical, NULL, &count, exts);
	printf("%u device extensions\n", count);
	const char* interesting[] = { "VK_EXT_provoking_vertex",
		"VK_EXT_transform_feedback", "VK_KHR_dynamic_rendering",
		"VK_EXT_multisampled_render_to_single_sampled",
		"VK_EXT_external_memory_host", "VK_KHR_external_memory_fd",
		"VK_EXT_external_memory_dma_buf", "VK_EXT_image_drm_format_modifier",
		"VK_KHR_swapchain", "VK_EXT_custom_border_color",
		"VK_EXT_extended_dynamic_state", "VK_EXT_vertex_attribute_divisor",
		"VK_EXT_index_type_uint8", "VK_EXT_line_rasterization",
		"VK_EXT_depth_clip_enable", "VK_EXT_host_query_reset" };
	for (unsigned i = 0; i < sizeof(interesting) / sizeof(interesting[0]); i++) {
		int found = 0;
		for (uint32_t j = 0; j < count; j++)
			found |= strcmp(exts[j].extensionName, interesting[i]) == 0;
		printf("  %-46s %s\n", interesting[i], found ? "yes" : "no");
	}

	VkPhysicalDeviceProvokingVertexFeaturesEXT provoking
		= { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT };
	VkPhysicalDeviceTransformFeedbackFeaturesEXT xfb
		= { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT };
	xfb.pNext = &provoking;
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	features.pNext = &xfb;
	vkGetPhysicalDeviceFeatures2(physical, &features);
	printf("provokingVertexLast %u, transformFeedback %u, "
		"vertexPipelineStoresAndAtomics %u, independentBlend %u\n",
		provoking.provokingVertexLast, xfb.transformFeedback,
		features.features.vertexPipelineStoresAndAtomics,
		features.features.independentBlend);

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physical, &props);
	VkPhysicalDeviceLimits* l = &props.limits;
	printf("standardSampleLocations %u, maxPerStageDescriptorUniformBuffers %u, "
		"maxPerStageDescriptorStorageBuffers %u, maxVertexOutputComponents %u, "
		"maxVertexInputAttributeOffset %u\n", l->standardSampleLocations,
		l->maxPerStageDescriptorUniformBuffers,
		l->maxPerStageDescriptorStorageBuffers, l->maxVertexOutputComponents,
		l->maxVertexInputAttributeOffset);
	return 0;
}
