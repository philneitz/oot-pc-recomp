"""Original Maxwell RED offset regression in coherent owned buffers; GPU execution requires --run.

Checks negative/zero/positive offsets, both 32-bit carry directions, signed 20-bit endpoints,
and the unsigned absolute-address meaning of RZ. No captured shader or game data is used.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess

import storage_candidates_fixture as fixture
from storage_candidates_gpu_test import spirv_requirements, storage

ROOT = Path(__file__).resolve().parents[1]
GUARD, POOL, UBO = 256, 4096, 65536
VALUE, INITIAL = 0x1234ABCD, 0x10203
CASES = [
    ('negative', 0x200000000, 0x20000000C, -12, False),
    ('zero', 0x200000000, 0x20000000C, 0, False),
    ('positive', 0x200000000, 0x20000000C, 20, False),
    ('borrow', 0x2FFFFFF00, 0x300000004, -12, False),
    ('carry', 0x2FFFFFF00, 0x2FFFFFFFC, 8, False),
    ('signed_min', 0x200000000, 0x200080020, -0x80000, False),
    ('signed_max', 0x200000000, 0x1FFF80021, 0x7FFFF, False),
    ('absolute_rz', 0xFF000, 0, 0xFFFF4, True),
]


def shader(offset, absolute):
    source = 255 if absolute else 0
    ops = [] if absolute else [fixture.movc(0, 0, 12), fixture.movc(1, 0, 13)]
    ops += [fixture.movc(2, 0, 14)]
    # RED.E.MAX.U32 [R0:R1 + signed offset], R2; RZ instead selects an unsigned absolute address.
    ops += [0xEBF8000000000000 | fixture.PT | 2 | (source << 8) | (2 << 23) |
            ((offset & 0xFFFFF) << 28) | (1 << 48), fixture.EXIT]
    words = []
    for at in range(0, len(ops), 3):
        group = ops[at:at + 3]
        words += [fixture.SCHED] + group + [0] * (3 - len(group))
    return struct.pack('<' + 'Q' * len(words), *words)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--translator', type=Path, required=True)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--vulkan-include', type=Path, required=True)
    parser.add_argument('--vulkan-library', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--device-name', default='')
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    runner = out / ('runner.exe' if os.name == 'nt' else 'runner')
    command = [args.cc, '-static', '-O1', '-std=c11', '-I', str(args.vulkan_include.resolve()),
               str(ROOT / 'tests/storage_candidates_gpu.c'), str(args.vulkan_library.resolve()), '-o', str(runner)]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
    report = {'gpu_execution_requested': args.run, 'cases': []}
    for name, base, pointer, immediate, absolute in CASES:
        folder = out / name
        folder.mkdir()
        code = folder / 'original.bin'
        code.write_bytes(shader(immediate, absolute))
        job = dict(key='red', stage='cs', code=str(code), debug=False, local=[1, 1, 1], shared=0,
                   c0_version=2, c0=[0, 0, 2, 0], c0_roots=[0] * 8,
                   sbuf_align=256, storage_candidates_version=1)
        translated = subprocess.run([str(args.translator.resolve()), '--serve'],
                                   input=json.dumps({'job': job, 'out': str(folder)}) + '\n',
                                   capture_output=True, text=True, timeout=60)
        (folder / 'translation.log').write_text(translated.stdout + translated.stderr)
        assert translated.returncode == 0 and translated.stdout.strip() == 'ok red', translated.stderr
        reflection = json.loads((folder / 'red.res.json').read_text())
        assert reflection['storage_candidates_version'] == 1
        requirements = spirv_requirements(folder / 'red.spv')
        buffers = [storage(POOL, 0x11), storage(POOL, 0x22), storage(UBO, 0),
                   storage(UBO, 0), storage(UBO, 0)]
        c0 = memoryview(buffers[3])[GUARD:GUARD + UBO]
        struct.pack_into('<QI', c0, 16, base, POOL)
        struct.pack_into('<Q', c0, 48, pointer)
        struct.pack_into('<I', c0, 56, VALUE)
        expected_address = immediate if absolute else pointer + immediate
        offset = expected_address - base
        assert 0 <= offset <= POOL - 4 and offset % 4 == 0
        struct.pack_into('<I', buffers[0], GUARD + offset, INITIAL)
        bindings = [(0, 0, 2, GUARD, UBO)]
        for resource in reflection['cbufs']:
            assert resource['slot'] in (0, 1)
            bindings.append((resource['binding'], 0, 3 + resource['slot'], GUARD, UBO))
        for resource in reflection['sbufs']:
            assert resource['cb_slot'] == 0 and resource['flags'] & 0x80
            address = struct.unpack_from('<Q', c0, resource['cb_offset'] * 4)[0] & ~255
            if base <= address < base + POOL:
                bindings.append((resource['binding'], 1, 0, GUARD + address - base, base + POOL - address))
            else:
                bindings.append((resource['binding'], 1, 2, GUARD, 1))
        features, subgroups, expected_bindings, _ = requirements
        assert sorted(x[0] for x in bindings) == expected_bindings
        initial = b''.join(buffers)
        packet = folder / 'packet.bin'
        packet.write_bytes(struct.pack('<13I', 0x53434750, 1, len(bindings), 5, features, subgroups,
                                       256, 0, *[len(x) for x in buffers]) +
                           b''.join(struct.pack('<5I', *x) for x in bindings) + initial)
        row = {'name': name, 'passed': None, 'expected_pool_offset': offset}
        if args.run:
            run = subprocess.run([str(runner), str(folder / 'red.spv'), str(packet),
                                  str(folder / 'readback.bin'), args.device_name, '1'],
                                 capture_output=True, text=True, timeout=35)
            (folder / 'gpu.log').write_text(run.stdout + run.stderr)
            device = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
            row.update(exit=run.returncode, device=device[-1] if device else None)
            expected = bytearray(initial)
            struct.pack_into('<I', expected, GUARD + offset, VALUE)
            got = (folder / 'readback.bin').read_bytes() if (folder / 'readback.bin').exists() else b''
            row['passed'] = bool(run.returncode == 0 and got == expected and device and
                                 device[-1]['validation_errors'] == 0 and device[-1]['validation_requested'] and
                                 device[-1]['validation_available'] and device[-1]['debug_messenger_created'] and
                                 device[-1]['robust_buffer_access_enabled'])
            row['all_other_bytes_unchanged'] = len(got) == len(expected) and got[:GUARD + offset] == expected[:GUARD + offset] and got[GUARD + offset + 4:] == expected[GUARD + offset + 4:]
            print(name, 'PASS' if row['passed'] else 'FAIL', flush=True)
        report['cases'].append(row)
        (out / 'report.json').write_text(json.dumps(report, indent=2))
        if args.run and row['exit'] != 0:
            break
    failures = sum(not row['passed'] for row in report['cases']) if args.run else 0
    print(f'RED offsets: {len(report["cases"])} cases, {failures} failures; GPU run {args.run}')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
