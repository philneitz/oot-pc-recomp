// Synthetic format compatibility regressions; no game content.
#include <assert.h>
#include <stdio.h>
#include "vk_view_format.h"

int main(void) {
    // Packed HDR outputs are written through a uint view of the same bits.
    assert(vk_color_view_compatible(VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_R32_UINT));
    assert(vk_color_view_compatible(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, VK_FORMAT_R32_UINT));
    assert(vk_color_view_compatible(VK_FORMAT_R16G16_UINT, VK_FORMAT_R32_SFLOAT));
    assert(vk_color_view_compatible(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB));
    // Equal byte counts do not permit crossing depth or compressed classes.
    assert(!vk_color_view_compatible(VK_FORMAT_D32_SFLOAT, VK_FORMAT_R32_UINT));
    assert(!vk_color_view_compatible(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_R32G32_UINT));
    assert(!vk_color_view_compatible(VK_FORMAT_R8_UNORM, VK_FORMAT_R32_UINT));
    assert(!vk_color_view_compatible(VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED));
    puts("Vulkan view format regressions passed");
    return 0;
}
