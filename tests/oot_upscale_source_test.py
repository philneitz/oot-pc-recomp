"""Exercise production upscaler source selection without a Vulkan device or game data."""
import argparse
import os
from pathlib import Path
import subprocess

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/clang.exe')
    args = parser.parse_args()
    out = ROOT / 'build/test/oot-upscale-source'
    out.mkdir(parents=True, exist_ok=True)
    production = function((ROOT / 'runtime/vk_exec_draw.c').read_text(), 'static void upscale_source_note(')
    prelude = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NVN_STAGE_CS 5
#define GFX_RES_IMG 1
#define GFX_RES_TEX 2
#define VK_IMAGE_TYPE_2D 1
#define VK_FORMAT_B10G11R11_UFLOAT_PACK32 122
#define VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 123
#define NVN_FORMAT_R32UI 11
#define NVN_FORMAT_R11G11B10F 63
typedef struct { int set_format, format; } view_t;
typedef struct { uint64_t image, addr; uint32_t width, height; int type, format; unsigned usage; } VkTexImage;
typedef struct { int slot; unsigned flags; } Resource;
typedef struct { int kind; Resource *res; } VkPipeBinding;
typedef struct { unsigned n_bindings; VkPipeBinding *bindings; } VkPipeInfo;
typedef struct { unsigned nstage; struct { uintptr_t data; } sd[1]; } Program;
typedef struct { Program *prog; uint64_t img[6][32], tex[6][128]; int tex_pool; } VkExec;
static struct { uint64_t image, addr; uint32_t w, h; int format; unsigned usage; int fresh, packed; } up_src;
static VkTexImage textures[4];
static int lock_depth, input_view_format;
static void vk_texture_lock(void) { ++lock_depth; }
static void vk_texture_unlock(void) { --lock_depth; }
static void rt_log(const char *format, ...) { (void)format; }
static void upscale_trace_image(VkExec *ex, const VkTexImage *t, int slot, unsigned flags) {
    (void)ex; (void)t; (void)slot; (void)flags;
}
static const VkTexImage *vk_texture_by_id_locked(const void *pool, int id, view_t *view, int *has_view) {
    (void)pool;
    if (lock_depth != 1 || id <= 0 || id >= 4) abort();
    if (view) *view = (view_t){id == 1 || (id == 2 && input_view_format), id == 1 ? NVN_FORMAT_R32UI : input_view_format};
    if (has_view) *has_view = id == 1 || (id == 2 && input_view_format);
    return &textures[id];
}
'''
    driver = r'''
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const char *test = argv[1];
    Program prog = {.nstage=1, .sd={{1}}};
    VkExec ex = {.prog=&prog};
    Resource output = {.flags=4}, input = {0}, second = {.slot=1};
    VkPipeBinding bindings[] = {{GFX_RES_IMG,&output}, {GFX_RES_TEX,&input}, {GFX_RES_TEX,&second}};
    VkPipeInfo info = {2, bindings};
    textures[1] = (VkTexImage){11,0x1000,1280,720,VK_IMAGE_TYPE_2D,VK_FORMAT_B10G11R11_UFLOAT_PACK32,7};
    textures[2] = (VkTexImage){22,0x2000,1280,720,VK_IMAGE_TYPE_2D,VK_FORMAT_B10G11R11_UFLOAT_PACK32,7};
    textures[3] = textures[2];
    ex.img[NVN_STAGE_CS][0] = 1;
    ex.tex[NVN_STAGE_CS][0] = 2;
    int want_float = 1;
    if (!strcmp(test,"off")) want_float = 0;
    if (!strcmp(test,"ambiguous")) {
        info.n_bindings=3; ex.tex[NVN_STAGE_CS][1]=3; textures[3].image=33; want_float=0;
    }
    if (!strcmp(test,"duplicate")) { info.n_bindings=3; ex.tex[NVN_STAGE_CS][1]=3; }
    if (!strcmp(test,"size")) { textures[2].width=640; want_float=0; }
    if (!strcmp(test,"format")) { textures[2].format=37; want_float=0; }
    if (!strcmp(test,"missing_program")) ex.prog=NULL;
    if (!strcmp(test,"packed_e5")) textures[1].format=VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
    if (!strcmp(test,"no_input")) { ex.tex[NVN_STAGE_CS][0]=0; want_float=0; }
    if (!strcmp(test,"same_image")) { textures[2].image=11; want_float=0; }
    if (!strcmp(test,"integer_input_view")) { input_view_format=NVN_FORMAT_R32UI; want_float=0; }
    if (!strcmp(test,"float_input_view")) input_view_format=NVN_FORMAT_R11G11B10F;
    if (!strcmp(test,"non_2d_input")) { textures[2].type=3; want_float=0; }
    if (!strcmp(test,"read_only_output")) { output.flags=0; want_float=-1; }
    upscale_source_note(&ex,&info);
    if (want_float == -1) return up_src.fresh || lock_depth;
    if (lock_depth || !up_src.fresh || up_src.image != (want_float ? 22u : 11u) ||
        up_src.addr != (want_float ? 0x2000u : 0x1000u) || up_src.packed != !want_float) {
        fprintf(stderr,"%s: wrong source image %llu packed %d lock %d\n",test,
                (unsigned long long)up_src.image,up_src.packed,lock_depth);
        return 1;
    }
    return 0;
}
'''
    source = out / 'source.c'
    source.write_text(prelude + production + driver)
    cases = ('known', 'variant', 'off', 'force', 'ambiguous', 'duplicate', 'size', 'format', 'missing_program', 'packed_e5', 'no_input', 'same_image',
             'integer_input_view', 'float_input_view', 'non_2d_input', 'read_only_output')
    for opt in ('-O0', '-O2'):
        exe = out / ('test.exe' if os.name == 'nt' else 'test')
        subprocess.run([args.cc, opt, '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(exe)], check=True)
        for case in cases:
            env = dict(os.environ)
            env.pop('OOT_LINEAR_UNPACKED_SOURCE', None)
            if case in ('off', 'force'):
                env['OOT_LINEAR_UNPACKED_SOURCE'] = '0' if case == 'off' else '1'
            subprocess.run([str(exe), case], env=env, check=True)
        print(f'{opt} production upscaler source: {len(cases)} cases passed')


if __name__ == '__main__':
    main()
