// The launcher's device check (DeviceCheck.java): opens Vulkan the way the game does (a chosen
// driver through the private loader, or the phone's own libvulkan.so), and lists what Xenia's
// Vulkan backend looks at (ui/vulkan/vulkan_device.cc, gpu/vulkan/*_cache.cc) as key=value
// lines; the Java side turns them into a verdict. libdaoprobe.so, in both APKs: it runs in its
// own process (:probe), so a driver that crashes takes only the check down.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <jni.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan_private_loader.h"

typedef struct
{
    char* text;
    size_t length, capacity;
} Out;

static void put(Out* o, const char* format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(line))
        n = sizeof(line) - 1;
    if (o->length + n + 1 > o->capacity)
    {
        size_t capacity = o->capacity ? o->capacity * 2 : 8192;
        while (capacity < o->length + n + 1)
            capacity *= 2;
        char* text = realloc(o->text, capacity);
        if (!text)
            return;
        o->text = text;
        o->capacity = capacity;
    }
    memcpy(o->text + o->length, line, n + 1);
    o->length += n;
}

static void put_version(Out* o, const char* key, uint32_t v)
{
    put(o, "%s=%u.%u.%u\n", key, VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
}

static int has_extension(const VkExtensionProperties* list, uint32_t count, const char* name)
{
    for (uint32_t i = 0; i < count; i++)
        if (!strcmp(list[i].extensionName, name))
            return 1;
    return 0;
}

// The device extensions Xenia uses or could use (vulkan_device.cc), listed as present or not.
static const char* const kExtensions[] = {
    "VK_KHR_swapchain",
    "VK_KHR_driver_properties",
    "VK_KHR_portability_subset",
    "VK_KHR_sampler_mirror_clamp_to_edge",
    "VK_KHR_shader_float_controls",
    "VK_EXT_fragment_shader_interlock",
    "VK_EXT_shader_demote_to_helper_invocation",
    "VK_EXT_sample_locations",
    "VK_EXT_non_seamless_cube_map",
    "VK_EXT_custom_border_color",
    "VK_EXT_conditional_rendering",
    "VK_KHR_fragment_shading_rate",
};

static void probe(Out* o, void* loader)
{
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(loader, "vkGetInstanceProcAddr");
    if (!gipa)
    {
        put(o, "error=no vkGetInstanceProcAddr in the Vulkan loader\n");
        return;
    }
    PFN_vkEnumerateInstanceVersion enumerate_version =
        (PFN_vkEnumerateInstanceVersion)gipa(NULL, "vkEnumerateInstanceVersion");
    uint32_t instance_version = VK_API_VERSION_1_0;
    if (enumerate_version)
        enumerate_version(&instance_version);
    put_version(o, "instance.version", instance_version);

    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "DAODroid device check";
    app.apiVersion = instance_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : instance_version;
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create_instance ? create_instance(&instance_info, NULL, &instance) : VK_ERROR_INITIALIZATION_FAILED;
    if (result != VK_SUCCESS)
    {
        put(o, "error=vkCreateInstance failed (%d)\n", (int)result);
        return;
    }

#define INSTANCE_FN(name) PFN_##name name = (PFN_##name)gipa(instance, #name)
    INSTANCE_FN(vkDestroyInstance);
    INSTANCE_FN(vkEnumeratePhysicalDevices);
    INSTANCE_FN(vkGetPhysicalDeviceProperties);
    INSTANCE_FN(vkGetPhysicalDeviceProperties2);
    INSTANCE_FN(vkGetPhysicalDeviceFeatures);
    INSTANCE_FN(vkGetPhysicalDeviceFeatures2);
    INSTANCE_FN(vkGetPhysicalDeviceFormatProperties);
    INSTANCE_FN(vkGetPhysicalDeviceMemoryProperties);
    INSTANCE_FN(vkEnumerateDeviceExtensionProperties);
#undef INSTANCE_FN

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, NULL);
    put(o, "device.count=%u\n", device_count);
    if (!device_count)
    {
        put(o, "error=no Vulkan device\n");
        vkDestroyInstance(instance, NULL);
        return;
    }
    // Android phones have one GPU; Xenia takes the first suitable one.
    VkPhysicalDevice devices[4];
    uint32_t taken = device_count < 4 ? device_count : 4;
    vkEnumeratePhysicalDevices(instance, &taken, devices);
    VkPhysicalDevice device = devices[0];

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    put(o, "device.name=%s\n", properties.deviceName);
    put_version(o, "device.api", properties.apiVersion);
    put(o, "device.driver_version=0x%08x\n", properties.driverVersion);
    put(o, "device.vendor=0x%04x\n", properties.vendorID);
    put(o, "device.id=0x%08x\n", properties.deviceID);
    put(o, "device.type=%d\n", (int)properties.deviceType);

    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, NULL);
    VkExtensionProperties* extensions = calloc(extension_count ? extension_count : 1, sizeof(*extensions));
    if (extensions)
        vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, extensions);
    else
        extension_count = 0;
    put(o, "ext.count=%u\n", extension_count);
    for (size_t i = 0; i < sizeof(kExtensions) / sizeof(kExtensions[0]); i++)
        put(o, "ext.%s=%d\n", kExtensions[i], has_extension(extensions, extension_count, kExtensions[i]));

    // The 1.1 queries need both the instance and the device at 1.1.
    const int queries2 = vkGetPhysicalDeviceFeatures2 && vkGetPhysicalDeviceProperties2 &&
                         app.apiVersion >= VK_API_VERSION_1_1 && properties.apiVersion >= VK_API_VERSION_1_1;
    const int driver_properties = queries2 && (properties.apiVersion >= VK_API_VERSION_1_2 ||
                                                  has_extension(extensions, extension_count, "VK_KHR_driver_properties"));
    if (driver_properties)
    {
        VkPhysicalDeviceDriverProperties driver = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 properties2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &driver};
        vkGetPhysicalDeviceProperties2(device, &properties2);
        put(o, "driver.id=%d\n", (int)driver.driverID);
        put(o, "driver.name=%s\n", driver.driverName);
        put(o, "driver.info=%s\n", driver.driverInfo);
    }

    VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT interlock = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT};
    VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    const int has_interlock = has_extension(extensions, extension_count, "VK_EXT_fragment_shader_interlock");
    if (queries2)
    {
        if (has_interlock)
            features.pNext = &interlock;
        vkGetPhysicalDeviceFeatures2(device, &features);
    }
    else
    {
        vkGetPhysicalDeviceFeatures(device, &features.features);
    }
    // vulkan_device.cc's list (with_gpu_emulation), plus BC texture compression.
#define FEATURE(name) put(o, "feature." #name "=%d\n", (int)features.features.name)
    FEATURE(robustBufferAccess);
    FEATURE(fullDrawIndexUint32);
    FEATURE(independentBlend);
    FEATURE(geometryShader);
    FEATURE(tessellationShader);
    FEATURE(sampleRateShading);
    FEATURE(depthClamp);
    FEATURE(fillModeNonSolid);
    FEATURE(samplerAnisotropy);
    FEATURE(occlusionQueryPrecise);
    FEATURE(vertexPipelineStoresAndAtomics);
    FEATURE(fragmentStoresAndAtomics);
    FEATURE(shaderClipDistance);
    FEATURE(shaderCullDistance);
    FEATURE(sparseBinding);
    FEATURE(sparseResidencyBuffer);
    FEATURE(textureCompressionBC);
    FEATURE(textureCompressionETC2);
    FEATURE(textureCompressionASTC_LDR);
#undef FEATURE
    put(o, "feature.fragmentShaderSampleInterlock=%d\n", (int)interlock.fragmentShaderSampleInterlock);
    put(o, "feature.fragmentShaderPixelInterlock=%d\n", (int)interlock.fragmentShaderPixelInterlock);

    // The limits the render target cache's FSI choice and the shared memory depend on.
    put(o, "limit.maxImageDimension2D=%u\n", properties.limits.maxImageDimension2D);
    put(o, "limit.maxStorageBufferRange=%u\n", properties.limits.maxStorageBufferRange);
    put(o, "limit.maxPerStageDescriptorStorageBuffers=%u\n", properties.limits.maxPerStageDescriptorStorageBuffers);
    put(o, "limit.maxPerStageDescriptorSampledImages=%u\n", properties.limits.maxPerStageDescriptorSampledImages);
    put(o, "limit.maxSamplerAnisotropy=%.0f\n", properties.limits.maxSamplerAnisotropy);
    put(o, "limit.standardSampleLocations=%d\n", (int)properties.limits.standardSampleLocations);

    // Sampled and linearly filtered, as vulkan_texture_cache.cc asks of the BC formats (it
    // decompresses to RGBA8 the ones that aren't).
    static const struct { const char* name; VkFormat format; VkFormatFeatureFlags needed; } kFormats[] = {
        {"BC1", VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT},
        {"BC2", VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT},
        {"BC3", VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT},
        {"BC4", VK_FORMAT_BC4_UNORM_BLOCK, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT},
        {"BC5", VK_FORMAT_BC5_UNORM_BLOCK, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT},
        {"D24S8", VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT},
        {"D32S8", VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT},
    };
    for (size_t i = 0; i < sizeof(kFormats) / sizeof(kFormats[0]); i++)
    {
        VkFormatProperties format;
        vkGetPhysicalDeviceFormatProperties(device, kFormats[i].format, &format);
        put(o, "format.%s=%d\n", kFormats[i].name,
            (format.optimalTilingFeatures & kFormats[i].needed) == kFormats[i].needed);
    }

    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(device, &memory);
    VkDeviceSize device_local = 0;
    for (uint32_t i = 0; i < memory.memoryHeapCount; i++)
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            device_local += memory.memoryHeaps[i].size;
    put(o, "memory.device_local_mb=%llu\n", (unsigned long long)(device_local >> 20));

    free(extensions);
    vkDestroyInstance(instance, NULL);
    put(o, "done=1\n");
}

JNIEXPORT jstring JNICALL Java_com_udarmolota_xdroid360_DeviceCheck_nativeProbe(JNIEnv* env, jclass cls, jstring driver_path,
                                                                    jstring hook_dir)
{
    (void)cls;
    Out o = {0};
    const char* driver = driver_path ? (*env)->GetStringUTFChars(env, driver_path, NULL) : NULL;
    const char* hooks = hook_dir ? (*env)->GetStringUTFChars(env, hook_dir, NULL) : NULL;
    // As the game: a chosen driver through the private loader, else the phone's own.
    void* loader = driver ? sod_open_private_vulkan_loader(driver, hooks) : dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    put(&o, "loader=%s\n", driver ? "private" : "system");
    if (loader)
        probe(&o, loader);
    else
        put(&o, "error=the Vulkan driver did not load (see logcat, tag DAODroid)\n");
    // The loader stays loaded: unloading a driver isn't safe on every phone.
    if (driver)
        (*env)->ReleaseStringUTFChars(env, driver_path, driver);
    if (hooks)
        (*env)->ReleaseStringUTFChars(env, hook_dir, hooks);
    jstring text = (*env)->NewStringUTF(env, o.text ? o.text : "error=out of memory\n");
    free(o.text);
    return text;
}
