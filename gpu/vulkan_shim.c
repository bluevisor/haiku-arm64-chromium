/*
 * libvulkan.so.1 for Haiku: a stand-in for the Vulkan loader, which has no
 * Haiku port. It exposes Mesa's Venus ICD (libvulkan_virtio.so, found
 * through LIBRARY_PATH) under the loader's entry point, which is all that
 * ANGLE's volk needs. Layers don't exist here, so their enumeration reports
 * none, as a loader without layers would.
 */

#include <string.h>

#include <vulkan/vulkan.h>

VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *version);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

static VKAPI_ATTR VkResult VKAPI_CALL
enumerate_instance_layers(uint32_t *count, VkLayerProperties *properties)
{
   *count = 0;
   return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL
enumerate_device_layers(VkPhysicalDevice device, uint32_t *count,
                        VkLayerProperties *properties)
{
   *count = 0;
   return VK_SUCCESS;
}

__attribute__((constructor)) static void
negotiate(void)
{
   /* the version a current loader asks for */
   uint32_t version = 5;
   vk_icdNegotiateLoaderICDInterfaceVersion(&version);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
   if (strcmp(name, "vkGetInstanceProcAddr") == 0)
      return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
   if (strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
      return (PFN_vkVoidFunction)enumerate_instance_layers;
   if (strcmp(name, "vkEnumerateDeviceLayerProperties") == 0)
      return (PFN_vkVoidFunction)enumerate_device_layers;
   return vk_icdGetInstanceProcAddr(instance, name);
}
