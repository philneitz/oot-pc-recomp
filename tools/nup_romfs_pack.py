"""Pack a NUP package's RomFS straight into the flat image and index.json the runtime reads.

`tools/extract_nup.py` then `tools/dkb_pack_romfs.py` needs room for the loose tree *and* the image: about 52 GB for
Ocarina of Time, whose RomFS is 26 GB. This writes the same image and index without the loose tree, so the peak is the
container plus the image. Each file is checked against the SHA-256 in the container while it streams.

    python3 tools/nup_romfs_pack.py <game>.nup --out build/extracted/oot111/romfs
    python3 tools/nup_romfs_pack.py <game>.nup --package update --out <dir>

The output is game data: keep it under build/. Standard library only.
"""
import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

from extract_nup import CHUNK, CODEC_RAW, Nup, NupError, Zstd, pick

ALIGN = 16


def raw_chunks(src, size):
    """Yield exactly `size` bytes of an uncompressed extent."""
    left = size
    while left:
        chunk = src.read(min(CHUNK, left))
        if not chunk:
            raise NupError('the container ends inside a file extent')
        left -= len(chunk)
        yield chunk


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('nup', type=Path, help='the .nup file')
    ap.add_argument('--out', type=Path, required=True, help='folder for romfs.bin and index.json')
    ap.add_argument('--package', default='update',
                    help='base, update (default), or an index from extract_nup.py --list')
    args = ap.parse_args(argv)

    try:
        nup = Nup(args.nup)
        pkg = pick(nup, args.package)
        zstd = Zstd()
        # Same order as dkb_pack_romfs.py: path bytes, so the image is deterministic whatever the container's order.
        entries = sorted((('/'.join(parts), idx) for parts, idx in nup.package_files(pkg, 'romfs')),
                         key=lambda e: e[0].encode())
        args.out.mkdir(parents=True, exist_ok=True)
        image = args.out / 'romfs.bin'
        sha = hashlib.sha256()
        files, off, t0 = [], 0, time.time()
        with open(image, 'wb') as dst:
            for i, (rel, idx) in enumerate(entries):
                name, size, ext_off, ext_size, want_sha, codec = nup.files[idx]
                pad = -off % ALIGN
                if pad:
                    dst.write(b'\0' * pad)
                    sha.update(b'\0' * pad)
                    off += pad
                h, written = hashlib.sha256(), 0
                nup.f.seek(ext_off)
                chunks = raw_chunks(nup.f, size) if codec == CODEC_RAW else zstd.stream(nup.f, ext_size)
                for chunk in chunks:
                    h.update(chunk)
                    dst.write(chunk)
                    sha.update(chunk)
                    written += len(chunk)
                    if written >= size:
                        break
                if written != size:
                    raise NupError(f'{rel}: expected {size} bytes, got {written}')
                if h.digest() != want_sha:
                    raise NupError(f'{rel}: SHA-256 differs from the container record (corrupt container)')
                files.append({'path': rel, 'offset': off, 'size': size})
                off += size
                if i % 5000 == 0 or i == len(entries) - 1:
                    rate = off / max(time.time() - t0, 1e-6) / 1e6
                    print(f'  {i + 1}/{len(entries)} files, {off / 2**30:.1f} GiB ({rate:.0f} MB/s)', file=sys.stderr)
        manifest = {'status': 'pass',
                    'source': f'packed from NUP package [{pkg.index}] {pkg.kind_name}; per-file SHA-256 checked',
                    'romfs_size': off, 'romfs_sha256': sha.hexdigest(), 'files': files}
        (args.out / 'index.json').write_text(json.dumps(manifest), encoding='utf-8')
        print(f'{len(files)} files, {off / 2**30:.2f} GiB, {time.time() - t0:.0f} s -> {image}')
        return 0
    except NupError as e:
        print(f'error: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
