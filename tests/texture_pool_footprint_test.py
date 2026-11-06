"""Check the production pool-image admission decision with synthetic Vulkan requirements.

No GPU or game data: oversized replacement formats must choose the existing own-allocation
fallback without binding over neighboring pool contents. Native-format alias policy stays intact.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]
PRELUDE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t VkImage;
typedef struct { int device, vma; } VkCtx;
typedef struct { uint64_t size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { uint64_t alloc_offset; uintptr_t alloc; } VkMemSpan;
typedef struct { uint32_t memoryType; } VmaAllocationInfo;
typedef struct { struct { uint64_t addr; } pub; struct { int vk, ideal, depth, stencil; } f;
                 uintptr_t bound_to; } Tex;
#define VK_SUCCESS 0
static VkMemoryRequirements requirements;
static VkMemSpan backing;
static uint32_t memory_type;
static int resolves, binds, available, bind_result;
static void vkGetImageMemoryRequirements(int d, VkImage i, VkMemoryRequirements *r) {
    (void)d; (void)i; *r = requirements;
}
static int vk_mem_resolve_existing(uint64_t a, uint64_t n, VkMemSpan *p) {
    (void)a; (void)n; resolves++; *p = backing; return available;
}
static void vmaGetAllocationInfo(int v, uintptr_t a, VmaAllocationInfo *i) {
    (void)v; (void)a; i->memoryType = memory_type;
}
static int vmaBindImageMemory2(int v, uintptr_t a, uint64_t o, VkImage i, void *p) {
    (void)v; (void)a; (void)o; (void)i; (void)p; binds++; return bind_result;
}
'''
CASES = r'''
static unsigned checks, failures;
#define CHECK(label, condition) do { checks++; if (!(condition)) { failures++; puts(label); } } while (0)
static void check(const char *name, int host, int ideal, int depth, int stencil,
                  uint64_t guest, uint64_t required, int expected) {
    Tex image = {.pub = {0x10000000}, .f = {host, ideal, depth, stencil}};
    VkCtx c = {0}; resolves = binds = 0;
    requirements = (VkMemoryRequirements){required, 256, 1};
    int result = bind_pool_memory(&c, &image, 7, guest);
    CHECK(name, result == expected);
    CHECK(name, expected ? image.bound_to == backing.alloc : image.bound_to == 0);
    CHECK(name, expected ? binds == 1 : binds == 0);
    // Footprint rejection must happen before even resolving a larger pool range.
    if (!expected) CHECK(name, resolves == 0);
}
int main(void) {
    backing = (VkMemSpan){0, 123}; available = 1;
    check("native colour fits", 1, 1, 0, 0, 4096, 4096, 1);
    check("native colour padding retains existing alias policy", 1, 1, 0, 0, 4096, 8192, 1);
    check("ASTC 4x4 decoded to RGBA8 cannot cover its neighbor", 2, 3, 0, 0, 4096, 16384, 0);
    check("ASTC 8x8 decoded to RGBA8 cannot cover its neighbors", 2, 4, 0, 0, 4096, 65536, 0);
    check("ASTC replacement by larger BC3 needs independent memory", 5, 4, 0, 0, 4096, 16384, 0);
    check("replacement format plus allocation padding must fit", 2, 3, 0, 0, 4095, 4096, 0);
    check("replacement format exactly fits", 2, 3, 0, 0, 4096, 4096, 1);
    check("smaller replacement format fits", 2, 3, 0, 0, 8192, 4096, 1);
    check("depth padding still rejects alias", 1, 1, 1, 0, 4096, 8192, 0);
    check("stencil padding still rejects alias", 1, 1, 0, 1, 4096, 8192, 0);
    check("zero requirement never binds", 1, 1, 0, 0, 4096, 0, 0);
    printf("texture pool footprint: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', default=os.environ.get('CC') or shutil.which('clang') or shutil.which('gcc'))
    p.add_argument('--source', type=Path, default=ROOT / 'runtime/vk_texture.c')
    p.add_argument('--out', type=Path, default=ROOT / 'build/test/texture-pool-footprint')
    a = p.parse_args()
    if not a.cc:
        p.error('pass --cc with a C compiler')
    a.out.mkdir(parents=True, exist_ok=True)
    source = a.out / 'footprint.c'
    source.write_text(PRELUDE + function(a.source.read_text(), 'static int bind_pool_memory(') + CASES)
    failures = 0
    for opt in ('-O0', '-O2'):
        exe = a.out / ('footprint' + opt + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([a.cc, '-std=c11', opt, '-Wall', '-Wextra', '-Werror', str(source), '-o', str(exe)], check=True)
        failures += subprocess.run([str(exe)]).returncode != 0
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
