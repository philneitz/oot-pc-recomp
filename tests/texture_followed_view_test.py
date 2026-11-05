"""Synthetic regressions through the production followed-texture resolver, without a GPU."""
from pathlib import Path
import os
import subprocess
import sys

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/oot-fix/followed-view-check'
PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vk.h"
#include "nvn_api.h"
#include "tex.h"
#include "vk_view_format.h"
typedef struct { uint64_t addr; } TexKey;
typedef struct Tex { VkTexImage pub; tex_t t; TexKey key; int released; struct Tex *next_at; } Tex;
typedef struct { tex_t t; view_t v; int has_view; } TexSlot;
static int by_key, by_addr;
static Tex live;
static void make_key(const tex_t *t, TexKey *key) { (void)t; memset(key,0,sizeof *key); }
static void *nvn_hm_get(const int *map,const void *key) { (void)key; return map==&by_addr ? &live : NULL; }
static int same_but_swizzle(const TexKey *a,const TexKey *b) { (void)a; (void)b; return 0; }
static Tex *entry_of(const tex_t *t) { (void)t; return NULL; }
int tex_vk_format(int format,TexVkFormat *out) {
    memset(out,0,sizeof *out);
    out->vk = format==NVN_FORMAT_RGB9E5F ? VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 :
              format==NVN_FORMAT_R16F ? VK_FORMAT_R16_SFLOAT : VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    return 1;
}
'''
TEST = r'''
int main(void) {
    live.t.d.format = NVN_FORMAT_R11G11B10F;
    live.pub.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    uint8_t identity[] = {NVN_TEXTURE_SWIZZLE_R,NVN_TEXTURE_SWIZZLE_G,NVN_TEXTURE_SWIZZLE_B,NVN_TEXTURE_SWIZZLE_A};
    memcpy(live.pub.swz,identity,4);
    TexSlot slot = {0};
    slot.t.d.format = NVN_FORMAT_RGB9E5F;
    slot.t.d.swizzle_r=identity[0]; slot.t.d.swizzle_g=identity[1];
    slot.t.d.swizzle_b=identity[2]; slot.t.d.swizzle_a=NVN_TEXTURE_SWIZZLE_ONE;
    view_t view;
    int has_view;
    assert(slot_resolve(&slot,&view,&has_view)==&live);
    assert(has_view && view.set_format && view.format==NVN_FORMAT_RGB9E5F);
    assert(view.set_swizzle && view.swz[3]==NVN_TEXTURE_SWIZZLE_ONE);
    slot.has_view=1; slot.v.set_format=1; slot.v.format=NVN_FORMAT_R32UI;
    slot_resolve(&slot,&view,&has_view);
    assert(has_view && view.set_format && view.format==NVN_FORMAT_R32UI);
    slot.has_view=0; memset(&slot.v,0,sizeof slot.v);
    slot.t.d.format=NVN_FORMAT_R11G11B10F; slot.t.d.swizzle_a=identity[3];
    slot_resolve(&slot,&view,&has_view);
    assert(!has_view && !view.set_format);
    slot.t.d.format=NVN_FORMAT_R16F;
    slot_resolve(&slot,&view,&has_view);
    assert(!has_view && !view.set_format);
    puts("Followed texture: implicit format and swizzle, explicit override and incompatible format checks passed");
    return 0;
}
'''

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    body = function((ROOT/'runtime/vk_texture.c').read_text(), 'static Tex *slot_resolve(')
    src = OUT/'followed-view.c'
    src.write_text(PRELUDE+'\n'+body+'\n'+TEST)
    cc = r'C:\msys64\ucrt64\bin\clang.exe'
    env = dict(os.environ)
    env['PATH'] = str(Path(cc).parent)+os.pathsep+env.get('PATH','')
    for opt in ('-O0','-O2'):
        exe=OUT/('followed-view'+opt+'.exe')
        subprocess.run([cc,opt,'-Wall','-Wextra','-Werror','-I',str(ROOT/'runtime'),
                        '-IC:/msys64/ucrt64/include/vma',str(src),'-o',str(exe)],check=True,env=env)
        subprocess.run([str(exe)],check=True,env=env)
    return 0

if __name__=='__main__':
    sys.exit(main())
