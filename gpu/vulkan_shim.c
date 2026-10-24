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

/* The ICD only hands out vkGetDeviceProcAddr for a live instance, but
 * loaders export it directly (zink dlsym()s it before creating one), so
 * remember the last instance created through us and resolve it lazily. */
static VkInstance last_instance;
static PFN_vkCreateInstance icd_create_instance;

static VKAPI_ATTR VkResult VKAPI_CALL
create_instance(const VkInstanceCreateInfo *info,
                const VkAllocationCallbacks *alloc, VkInstance *instance)
{
   VkResult result = icd_create_instance(info, alloc, instance);
   if (result == VK_SUCCESS)
      __atomic_store_n(&last_instance, *instance, __ATOMIC_RELEASE);
   return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetDeviceProcAddr(VkDevice device, const char *name)
{
   static PFN_vkGetDeviceProcAddr icd_get_device_proc_addr;
   if (icd_get_device_proc_addr == NULL) {
      VkInstance instance = __atomic_load_n(&last_instance, __ATOMIC_ACQUIRE);
      if (instance == NULL)
         return NULL;
      icd_get_device_proc_addr = (PFN_vkGetDeviceProcAddr)
         vk_icdGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");
      if (icd_get_device_proc_addr == NULL)
         return NULL;
   }
   return icd_get_device_proc_addr(device, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
   if (strcmp(name, "vkGetInstanceProcAddr") == 0)
      return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
   if (strcmp(name, "vkCreateInstance") == 0) {
      icd_create_instance = (PFN_vkCreateInstance)
         vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance");
      return icd_create_instance ? (PFN_vkVoidFunction)create_instance : NULL;
   }
   if (strcmp(name, "vkEnumerateInstanceLayerProperties") == 0)
      return (PFN_vkVoidFunction)enumerate_instance_layers;
   if (strcmp(name, "vkEnumerateDeviceLayerProperties") == 0)
      return (PFN_vkVoidFunction)enumerate_device_layers;
   return vk_icdGetInstanceProcAddr(instance, name);
}
