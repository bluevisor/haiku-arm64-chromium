// Asks a host Vulkan driver (through UTM's loader) about the features ANGLE
// needs for ES 3.0. Pick the driver with VK_DRIVER_FILES.
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

int
main(int argc, char** argv)
{
	const char* loader = argc > 1 ? argv[1]
		: "/Applications/UTM.app/Contents/Frameworks/vulkan.1.framework/vulkan.1";
	void* lib = dlopen(loader, RTLD_NOW);
	if (lib == NULL) {
		printf("FAIL dlopen: %s\n", dlerror());
		return 1;
	}
	PFN_vkGetInstanceProcAddr gipa
		= (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
	PFN_vkCreateInstance createInstance
		= (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");

	const char* instanceExts[] = { "VK_KHR_portability_enumeration",
		"VK_KHR_get_physical_device_properties2" };
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	ici.pApplicationInfo = &app;
	ici.enabledExtensionCount = 2;
	ici.ppEnabledExtensionNames = instanceExts;
	VkInstance instance;
	VkResult result = createInstance(&ici, NULL, &instance);
	if (result != VK_SUCCESS) {
		printf("FAIL vkCreateInstance %d\n", result);
		return 1;
	}
#define I(name) PFN_##name name = (PFN_##name)gipa(instance, #name)
	I(vkEnumeratePhysicalDevices);
	I(vkEnumerateDeviceExtensionProperties);
	I(vkGetPhysicalDeviceProperties2);
	I(vkGetPhysicalDeviceFeatures2);
	uint32_t count = 1;
	VkPhysicalDevice physical;
	vkEnumeratePhysicalDevices(instance, &count, &physical);
	if (count == 0) {
		printf("FAIL: no devices\n");
		return 1;
	}

	VkPhysicalDeviceDriverProperties driver
		= { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
	VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	props.pNext = &driver;
	vkGetPhysicalDeviceProperties2(physical, &props);
	printf("device %s, driver %s (%s), api %u.%u.%u\n", props.properties.deviceName,
		driver.driverName, driver.driverInfo,
		VK_API_VERSION_MAJOR(props.properties.apiVersion),
		VK_API_VERSION_MINOR(props.properties.apiVersion),
		VK_API_VERSION_PATCH(props.properties.apiVersion));

	VkExtensionProperties exts[512];
	count = 512;
	vkEnumerateDeviceExtensionProperties(physical, NULL, &count, exts);
	int provoking = 0, xfb = 0;
	for (uint32_t i = 0; i < count; i++) {
		provoking |= strcmp(exts[i].extensionName, "VK_EXT_provoking_vertex") == 0;
		xfb |= strcmp(exts[i].extensionName, "VK_EXT_transform_feedback") == 0;
	}
	printf("%u device extensions; VK_EXT_provoking_vertex %s, "
		"VK_EXT_transform_feedback %s\n", count, provoking ? "yes" : "no",
		xfb ? "yes" : "no");

	VkPhysicalDeviceProvokingVertexFeaturesEXT pv
		= { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT };
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	if (provoking)
		features.pNext = &pv;
	vkGetPhysicalDeviceFeatures2(physical, &features);
	printf("provokingVertexLast %u, geometryShader %u, independentBlend %u, "
		"standardSampleLocations %u\n", pv.provokingVertexLast,
		features.features.geometryShader, features.features.independentBlend,
		props.properties.limits.standardSampleLocations);
	VkPhysicalDeviceLimits* l = &props.properties.limits;
	printf("vertexPipelineStoresAndAtomics %u, fragmentStoresAndAtomics %u, "
		"maxPerStageDescriptorUniformBuffers %u, maxPerStageDescriptorStorageBuffers %u, "
		"maxVertexOutputComponents %u, maxVertexInputAttributeOffset %u\n",
		features.features.vertexPipelineStoresAndAtomics,
		features.features.fragmentStoresAndAtomics,
		l->maxPerStageDescriptorUniformBuffers, l->maxPerStageDescriptorStorageBuffers,
		l->maxVertexOutputComponents, l->maxVertexInputAttributeOffset);
	return 0;
}
