// Lists a host Vulkan driver's device extensions and memory types natively
// (through UTM's loader; pick the driver with VK_DRIVER_FILES).
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

int
main(void)
{
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
	app.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	ici.pApplicationInfo = &app;
	ici.enabledExtensionCount = 2;
	ici.ppEnabledExtensionNames = instanceExts;
	VkInstance instance;
	if (vkCreateInstance(&ici, NULL, &instance) != VK_SUCCESS) {
		printf("FAIL vkCreateInstance\n");
		return 1;
	}
#define I(n) PFN_##n n = (PFN_##n)gipa(instance, #n)
	I(vkEnumeratePhysicalDevices); I(vkEnumerateDeviceExtensionProperties);
	I(vkGetPhysicalDeviceMemoryProperties); I(vkGetPhysicalDeviceProperties2);
	uint32_t count = 1;
	VkPhysicalDevice physical;
	vkEnumeratePhysicalDevices(instance, &count, &physical);
	VkPhysicalDeviceExternalMemoryHostPropertiesEXT host
		= { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT };
	VkPhysicalDeviceProperties2 props = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	props.pNext = &host;
	vkGetPhysicalDeviceProperties2(physical, &props);
	printf("device %s\nminImportedHostPointerAlignment %llu\n", props.properties.deviceName,
		(unsigned long long)host.minImportedHostPointerAlignment);
	VkPhysicalDeviceMemoryProperties mem;
	vkGetPhysicalDeviceMemoryProperties(physical, &mem);
	for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
		printf("memory type %u: flags %#x heap %u\n", i, mem.memoryTypes[i].propertyFlags,
			mem.memoryTypes[i].heapIndex);
	VkExtensionProperties exts[512];
	count = 512;
	vkEnumerateDeviceExtensionProperties(physical, NULL, &count, exts);
	printf("%u device extensions:\n", count);
	for (uint32_t i = 0; i < count; i++)
		printf("  %s\n", exts[i].extensionName);
	return 0;
}
