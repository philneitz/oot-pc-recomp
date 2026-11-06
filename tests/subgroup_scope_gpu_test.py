"""Original divergent-lane ballot regression; GPU execution requires --run.

Every lane evaluates a ballot before a branch. Selected lanes consume its population count
after the branch. Moving the ballot to its use incorrectly changes the participating lanes.
No captured shader or guest memory is used. All non-output bytes are checked unchanged.
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
CASES = [
    ('lower_half', 16, True, 'all'),
    ('upper_half', 16, False, 'all'),
    ('lower_quarter', 8, True, 'all'),
    ('upper_three_quarters', 8, False, 'all'),
    ('upper_half_predicate', 16, False, 'predicate'),
    ('lower_half_inverse', 16, True, 'inverse'),
    ('uniform_lower', 32, True, 'all'),
    ('uniform_upper', 0, False, 'all'),
]


def shader(limit, lower, source):
    source_predicate = 7 if source == 'all' else 0
    # S2R R0, SR_LANEID; ISETP.LT.U32 P0, PT, R0, limit, PT; VOTE R2, PT/P0/!P0.
    ops = [0xF0C8000000000000 | fixture.PT,
           0x3660000000000000 | fixture.PT | 7 | (limit << 20) | (7 << 39) | (1 << 49),
           0x50D8000000000000 | fixture.PT | 2 | (source_predicate << 39) |
           ((source == 'inverse') << 42) | (7 << 45) | (1 << 48),
           0,
           0x5C08000000000000 | fixture.PT | 3 | (2 << 20),  # POPC R3, R2
           0x3848000000000000 | fixture.PT | 6 | (2 << 20),  # SHL R6, R0, 2
           fixture.movc(4, 0, 4), fixture.movc(5, 0, 5),
           fixture.iaddr(4, 4, 6, cc=True), fixture.iaddr(5, 5, 255, carry=True),
           fixture.global_memory(True, 3, 4), fixture.EXIT]
    # BRA to EXIT skips the non-selected lanes. Account for Maxwell's scheduling words.
    address = lambda index: index // 3 * 32 + 8 + index % 3 * 8
    relative = address(11) - address(3) - 8
    ops[3] = 0xE24000000000000F | (int(lower) << 19) | ((relative & 0xFFFFFF) << 20)
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
    parser.add_argument('--spirv-val', default='spirv-val')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--device-name', default='')
    parser.add_argument('--debug', action='store_true', help='Control: disable AST propagation through translator debug mode')
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    runner = out / ('runner.exe' if os.name == 'nt' else 'runner')
    command = [args.cc, '-static', '-O1', '-std=c11', '-I', str(args.vulkan_include.resolve()),
               str(ROOT / 'tests/storage_candidates_gpu.c'), str(args.vulkan_library.resolve()), '-o', str(runner)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (out / 'compile.log').write_text(compiled.stdout + compiled.stderr)
    assert compiled.returncode == 0, compiled.stderr
    report = {'gpu_execution_requested': args.run, 'debug': args.debug, 'cases': []}
    for name, limit, lower, source in CASES:
        folder = out / name
        folder.mkdir()
        code = folder / 'original.bin'
        code.write_bytes(shader(limit, lower, source))
        job = dict(key='scope', stage='cs', debug=args.debug, code=str(code), local=[32, 1, 1], shared=0,
                   c0_version=2, c0=[0, 0, 1, 0], c0_roots=[0] * 8, sbuf_align=256,
                   storage_candidates_version=1)
        translated = subprocess.run([str(args.translator.resolve()), '--serve'],
                                   input=json.dumps({'job': job, 'out': str(folder)}) + '\n',
                                   capture_output=True, text=True, timeout=60)
        (folder / 'translation.log').write_text(translated.stdout + translated.stderr)
        assert translated.returncode == 0 and translated.stdout.strip() == 'ok scope', translated.stderr
        subprocess.run([args.spirv_val, '--target-env', 'vulkan1.3', str(folder / 'scope.spv')], check=True)
        reflection = json.loads((folder / 'scope.res.json').read_text())
        assert not reflection['textures'] and not reflection['images']
        buffers = [storage(POOL, 0x11), storage(POOL, 0x22), storage(UBO, 0),
                   storage(UBO, 0), storage(UBO, 0)]
        struct.pack_into('<QI', buffers[3], GUARD + 16, 0x200000000, POOL)
        bindings = [(0, 0, 2, GUARD, UBO)]
        for resource in reflection['cbufs']:
            assert resource['slot'] == 0
            bindings.append((resource['binding'], 0, 3, GUARD, UBO))
        for resource in reflection['sbufs']:
            assert resource['cb_slot'] == 0 and resource['cb_offset'] == 4 and resource['flags'] & 0x80
            bindings.append((resource['binding'], 1, 0, GUARD, POOL))
        features, subgroups, spv_bindings, _ = spirv_requirements(folder / 'scope.spv')
        assert sorted(x[0] for x in bindings) == spv_bindings
        initial = b''.join(buffers)
        packet = folder / 'packet.bin'
        packet.write_bytes(struct.pack('<13I', 0x53434750, 1, len(bindings), 5, features, subgroups,
                                       256, 0, *[len(x) for x in buffers]) +
                           b''.join(struct.pack('<5I', *x) for x in bindings) + initial)
        count = 32 if source == 'all' else limit if source == 'predicate' else 32 - limit
        expected = bytearray(initial)
        for lane in range(32):
            if (lane < limit) == lower:
                struct.pack_into('<I', expected, GUARD + lane * 4, count)
        row = {'name': name, 'passed': None, 'expected_count': count}
        if args.run:
            run = subprocess.run([str(runner), str(folder / 'scope.spv'), str(packet),
                                  str(folder / 'readback.bin'), args.device_name, '1'],
                                 capture_output=True, text=True, timeout=35)
            (folder / 'gpu.log').write_text(run.stdout + run.stderr)
            device = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
            got = (folder / 'readback.bin').read_bytes() if (folder / 'readback.bin').exists() else b''
            row.update(exit=run.returncode, device=device[-1] if device else None,
                       actual_words=list(struct.unpack_from('<32I', got, GUARD)) if got else [])
            row['passed'] = bool(run.returncode == 0 and got == expected and device and
                                 device[-1]['validation_errors'] == 0 and device[-1]['validation_requested'] and
                                 device[-1]['validation_available'] and device[-1]['debug_messenger_created'] and
                                 device[-1]['robust_buffer_access_enabled'])
            print(name, 'PASS' if row['passed'] else 'FAIL', flush=True)
        report['cases'].append(row)
        (out / 'report.json').write_text(json.dumps(report, indent=2))
        if args.run and row['exit'] != 0:
            break
    failures = sum(not row['passed'] for row in report['cases']) if args.run else 0
    print(f'Subgroup scope: {len(report["cases"])} cases, {failures} failures; GPU run {args.run}')
    return int(bool(failures))


if __name__ == '__main__':
    raise SystemExit(main())
