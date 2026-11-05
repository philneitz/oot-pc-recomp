"""Write a RomFS overlay with shortened copies of Ocarina of Time's cutscene videos, to reach gameplay faster in test runs.

    python tools/oot_short_movie.py --romfs build/extracted/oot111/romfs --out build/oot111/romfs-test --seconds 5 <movie path in the RomFS>

Each copy keeps the clusters that start before --seconds: the header stays byte for byte except the Segment size and the
Duration, and the SeekHead becomes a Void because its Cues are gone. The originals aren't touched. The overlay is game data:
it stays under build/, and only a run with TOTK_ROMFS_PATCHES=<out> uses it (OoT-Testlauf.ps1). Standard library only.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

EBML, SEGMENT, SEEKHEAD, INFO, CLUSTER = 0x1A45DFA3, 0x18538067, 0x114D9B74, 0x1549A966, 0x1F43B675
DURATION, TIMESTAMP, SCALE = 0x4489, 0xE7, 0x2AD7B1


def elem_id(b, at):
    n = 1
    mask = 0x80
    while n <= 4 and not b[at] & mask:
        n += 1
        mask >>= 1
    return int.from_bytes(b[at:at + n], 'big'), n


def elem_size(b, at):
    n = 1
    mask = 0x80
    while n <= 8 and not b[at] & mask:
        n += 1
        mask >>= 1
    return int.from_bytes(b[at:at + n], 'big') & ((1 << (7 * n)) - 1), n


def children(b, start, end):
    at = start
    while at < end:
        i, n = elem_id(b, at)
        s, m = elem_size(b, at + n)
        yield i, at, at + n + m, s
        at += n + m + s


def shorten(data, seconds):
    out = bytearray(data)
    top = list(children(data, 0, len(data)))
    if top[0][0] != EBML or top[1][0] != SEGMENT:
        raise SystemExit('not a WebM with a sized Segment')
    _, seg_at, seg_body, seg_size = top[1]
    scale, duration_at, seekhead, cut = 1000000, None, None, None
    for i, at, body, s in children(data, seg_body, seg_body + seg_size):
        if i == SEEKHEAD:
            seekhead = (at, body + s)
        elif i == INFO:
            for j, _, jb, js in children(data, body, body + s):
                if j == SCALE:
                    scale = int.from_bytes(data[jb:jb + js], 'big')
                elif j == DURATION:
                    if js != 8:
                        raise SystemExit('Duration is not a double')
                    duration_at = jb
        elif i == CLUSTER:
            for j, _, jb, js in children(data, body, min(body + 64, body + s)):
                if j == TIMESTAMP:
                    stamp = int.from_bytes(data[jb:jb + js], 'big') * scale / 1e9
                    if stamp >= seconds:
                        cut = (at, stamp)
                    break
            if cut:
                break
    if not cut or duration_at is None or not seekhead:
        raise SystemExit('the layout is not as expected, or the video is shorter than --seconds')
    at, stamp = cut
    del out[at:]
    out[duration_at:duration_at + 8] = struct.pack('>d', stamp * 1e9 / scale)
    a, b = seekhead
    if not 2 <= b - a <= 127:
        raise SystemExit('unexpected SeekHead size')
    out[a:b] = bytes([0xEC, 0x80 | (b - a - 2)]) + bytes(b - a - 2)
    _, n = elem_id(data, seg_at)
    _, m = elem_size(data, seg_at + n)
    out[seg_at + n:seg_at + n + m] = (0x01 << (8 * m - m) | (at - seg_body)).to_bytes(m, 'big')
    return bytes(out), stamp


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--romfs', type=Path, required=True, help='folder with romfs.bin and index.json')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--seconds', type=float, default=5)
    ap.add_argument('paths', nargs='+')
    args = ap.parse_args()
    index = {e['path']: e for e in json.loads((args.romfs / 'index.json').read_text(encoding='utf-8'))['files']}
    lines = []
    with open(args.romfs / 'romfs.bin', 'rb') as f:
        for path in args.paths:
            e = index[path]
            f.seek(e['offset'])
            data = f.read(e['size'])
            short, stamp = shorten(data, args.seconds)
            dest = args.out / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(short)
            lines.append(f"{path}\t{len(short)}\t{hashlib.sha256(short).hexdigest()}\t{hashlib.sha256(data).hexdigest()}\n")
            print(f'{path}: {len(data) / 1e6:.1f} MB -> {len(short) / 1e6:.1f} MB, {stamp:.2f} s')
    (args.out / 'manifest.tsv').write_text(''.join(lines), encoding='ascii', newline='\n')


if __name__ == '__main__':
    main()
