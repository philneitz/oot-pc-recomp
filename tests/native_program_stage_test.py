"""Exercise production stage lookup with compact native controls against a guard page."""
from pathlib import Path
import os
import subprocess

from nvn2_compute_c0_test import function

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/oot-fix/native-stage-check'
PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
typedef struct { uint64_t data; const unsigned char *control; } NVNshaderData;
enum { NVN_STAGE_VS=0, NVN_STAGE_FS=1, NVN_STAGE_GS=2, NVN_STAGE_CS=5 };
static int gfx_shader_is_nvn2(const uint8_t *ctl) { return ctl[0] == 0x42; }
'''
TEST = r'''
int main(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    size_t page = info.dwPageSize;
    uint8_t *memory = VirtualAlloc(NULL, page*2, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    assert(memory);
    DWORD old;
    assert(VirtualProtect(memory+page, page, PAGE_NOACCESS, &old));
    uint8_t *compact = memory+page-0xd8;
    compact[0] = 0x42;
    uint32_t size=0x100, word, stage, length;
    memcpy(compact+0x38, &size, 4);
    NVNshaderData data = {(uint64_t)(uintptr_t)&word, compact};
    unsigned expected[] = {0,0,3,4,2,1};
    for (unsigned type=1; type<=5; type++) {
        word=1|(4<<5)|(type<<10);
        assert(program_stage_info(&data,&stage,&length));
        assert(stage==expected[type] && length==size);
    }
    word=0;
    assert(program_stage_info(&data,&stage,&length) && stage==NVN_STAGE_CS);
    word=1|(4<<5)|(6<<10);
    assert(!program_stage_info(&data,&stage,&length));
    size=0;
    memcpy(compact+0x38,&size,4);
    assert(!program_stage_info(&data,&stage,&length));
    uint8_t legacy[0x880]={0};
    size=0x456;
    stage=NVN_STAGE_FS;
    memcpy(legacy+0x704,&size,4);
    memcpy(legacy+0x714,&stage,4);
    data.control=legacy;
    assert(program_stage_info(&data,&stage,&length));
    assert(stage==NVN_STAGE_FS && length==size);
    data.control=NULL;
    assert(!program_stage_info(&data,&stage,&length));
    data.control=compact;
    data.data=0;
    assert(!program_stage_info(&data,&stage,&length));
    VirtualFree(memory,0,MEM_RELEASE);
    puts("native and legacy stage metadata: guard-page checks passed");
}
'''

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'runtime/vk_exec_draw.c').read_text()
    fixture = OUT / 'stage.c'
    fixture.write_text(PRELUDE + 'static int ' + function(source, 'program_stage_info') + TEST)
    compiler = os.environ.get('CC', r'C:\msys64\ucrt64\bin\clang.exe')
    for optimization in ('-O0', '-O2'):
        exe = OUT / ('stage' + optimization + '.exe')
        subprocess.run([compiler, optimization, str(fixture), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
