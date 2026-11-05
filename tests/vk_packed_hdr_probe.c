// Synthetic GPU regression: preserve RGB9E5 bits written into 32-bit HDR storage,
// then convert the colour values. No game content or window system is required.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define VK_CHECK(call) do { VkResult result = (call); if (result != VK_SUCCESS) { \
    fprintf(stderr, "%s failed: %d\n", #call, result); return 1; } } while (0)

static uint32_t memory_type(VkPhysicalDevice gpu, uint32_t mask, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(gpu, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; i++)
        if ((mask & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    assert(!"required memory type is unavailable");
    return 0;
}

int main(void) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&ici, NULL, &instance));
    VkPhysicalDevice gpus[16], gpu = VK_NULL_HANDLE;
    uint32_t count = 16;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, gpus));
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(gpus[i], &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { gpu = gpus[i]; break; }
    }
    assert(gpu);
    VkQueueFamilyProperties families[64];
    count = 64;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families);
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) family++;
    assert(family < count);
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
    VkDevice device;
    VK_CHECK(vkCreateDevice(gpu, &dci, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkFormat formats[] = {VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, VK_FORMAT_R32G32B32A32_SFLOAT};
    VkImage images[3];
    VkDeviceMemory image_memory[3];
    for (int i = 0; i < 3; i++) {
        VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
            .format = formats[i], .extent = {1, 1, 1}, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
        VK_CHECK(vkCreateImage(device, &ii, NULL, &images[i]));
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, images[i], &requirements);
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        VK_CHECK(vkAllocateMemory(device, &ai, NULL, &image_memory[i]));
        VK_CHECK(vkBindImageMemory(device, images[i], image_memory[i], 0));
    }
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 256,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer buffer;
    VkDeviceMemory buffer_memory;
    VK_CHECK(vkCreateBuffer(device, &bi, NULL, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type(gpu, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VK_CHECK(vkAllocateMemory(device, &ai, NULL, &buffer_memory));
    VK_CHECK(vkBindBufferMemory(device, buffer, buffer_memory, 0));
    void *mapped;
    VK_CHECK(vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    uint32_t packed = (16u << 27) | (256u << 18) | (128u << 9) | 64u; // RGB = 0.25, 0.5, 1
    memcpy(mapped, &packed, sizeof packed);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    VkCommandPool pool;
    VK_CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cb;
    VK_CHECK(vkAllocateCommandBuffers(device, &cai, &cb));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cb, &begin));
    VkImageMemoryBarrier barriers[3];
    for (int i = 0; i < 3; i++) barriers[i] = (VkImageMemoryBarrier){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = images[i], .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 3, barriers);
    VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {1, 1, 1}};
    vkCmdCopyBufferToImage(cb, buffer, images[0], VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    VkMemoryBarrier memory = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory, 0, NULL, 0, NULL);
    VkImageCopy copy = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .extent = {1, 1, 1}};
    vkCmdCopyImage(cb, images[0], VK_IMAGE_LAYOUT_GENERAL, images[1], VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory, 0, NULL, 0, NULL);
    VkImageBlit blit = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .srcOffsets = {{0, 0, 0}, {1, 1, 1}},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .dstOffsets = {{0, 0, 0}, {1, 1, 1}}};
    vkCmdBlitImage(cb, images[1], VK_IMAGE_LAYOUT_GENERAL, images[2], VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory, 0, NULL, 0, NULL);
    region.bufferOffset = 16;
    vkCmdCopyImageToBuffer(cb, images[2], VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &region);
    memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory, 0, NULL, 0, NULL);
    VK_CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    float rgba[4];
    memcpy(rgba, (char *)mapped + 16, sizeof rgba);
    printf("RGB9E5 bit reinterpretation: %.6f %.6f %.6f %.6f\n", rgba[0], rgba[1], rgba[2], rgba[3]);
    assert(fabsf(rgba[0] - 0.25f) < 1e-5f && fabsf(rgba[1] - 0.5f) < 1e-5f && fabsf(rgba[2] - 1) < 1e-5f && rgba[3] == 1);
    vkUnmapMemory(device, buffer_memory);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, buffer_memory, NULL);
    for (int i = 0; i < 3; i++) { vkDestroyImage(device, images[i], NULL); vkFreeMemory(device, image_memory[i], NULL); }
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
