#!/usr/bin/env bash
# Ocarina of Time 1.1.1 (Switch 2) Linux build.
#
#   ./linux/build-oot.sh --nup <game>.nup          # container: extracts the update package itself
#   ./linux/build-oot.sh --dump <dir>              # <dir>/exefs (plain NSOs) and <dir>/romfs
#   ./linux/build-oot.sh --exefs <dir> --romfs <dir>
#
# Options: --jobs N, --skip-lift (reuse generated/oot111), --skip-build, --rebuild-xlate.
#
# --nup needs the ZBIC decoder build/zbic/zbic_dec for a 22.0.0+ dump; tools/nso_plain.py explains it. A RomFS folder
# may also be a packed romfs.bin + index.json. Nothing from the game is written outside build/ and generated/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION=oot111
GEN="$ROOT/generated/$VERSION"
OUT="$ROOT/build/$VERSION"
EXTRACTED="$ROOT/build/extracted/$VERSION"
PY="$ROOT/.venv/bin/python"
SYS_PY="$(command -v python3)"
EXPECTED="$ROOT/tools/data/expected-oot111.json"

NUP="" DUMP="" EXEFS="" ROMFS="" JOBS="" SKIP_LIFT=0 SKIP_BUILD=0 REBUILD_XLATE=0

die()  { printf 'error: %s\n' "$*" >&2; exit 1; }
note() { printf '==> %s\n' "$*"; }

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }

while (($#)); do
  case "$1" in
    --nup) NUP="${2:?--nup needs a file}"; shift 2 ;;
    --dump) DUMP="${2:?--dump needs a folder}"; shift 2 ;;
    --exefs) EXEFS="${2:?--exefs needs a folder}"; shift 2 ;;
    --romfs) ROMFS="${2:?--romfs needs a folder}"; shift 2 ;;
    --jobs) JOBS="${2:?--jobs needs a number}"; shift 2 ;;
    --skip-lift) SKIP_LIFT=1; shift ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --rebuild-xlate) REBUILD_XLATE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1 (use --help)" ;;
  esac
done

[[ -n "$NUP" || -n "$DUMP" || ( -n "$EXEFS" && -n "$ROMFS" ) ]] || \
  die "pass --nup FILE, --dump DIR or --exefs DIR --romfs DIR"

if [[ -n "$DUMP" ]]; then
  [[ -d "$DUMP/exefs" && -d "$DUMP/romfs" ]] || die "$DUMP has no exefs/ and romfs/"
  EXEFS="$DUMP/exefs"; ROMFS="$DUMP/romfs"
fi

note "checking the host toolchain"
for tool in clang lld ninja dotnet; do
  command -v "$tool" >/dev/null 2>&1 || die "'$tool' not on PATH (docs/building.md, Linux)"
done
for lib in sdl3 opus vpx libzstd dav1d vulkan; do
  pkg-config --exists "$lib" 2>/dev/null || die "pkg-config cannot find $lib (install its -dev package)"
done
grep -qw avx2 /proc/cpuinfo || die "the CPU does not report AVX2"
[[ -x "$PY" ]] || die ".venv missing: uv venv --python 3.12 .venv && uv pip install --python .venv/bin/python -r tools/requirements.txt"
[[ -n "$JOBS" ]] || JOBS=$(( $(nproc) > 2 ? $(nproc) - 1 : 1 ))

# ---------------------------------------------------------------- game files
if [[ -n "$NUP" ]]; then
  note "extracting the update ExeFS from $NUP"
  "$SYS_PY" tools/extract_nup.py "$NUP" --package update --only exefs --out "$EXTRACTED"
  EXEFS="$EXTRACTED/exefs"
  [[ -x "$ROOT/build/zbic/zbic_dec" ]] || \
    die "build/zbic/zbic_dec missing (needed for a ZBIC 22.0.0+ dump; see tools/nso_plain.py)"
  note "converting ZBIC NSOs to plain"
  "$SYS_PY" tools/nso_plain.py "$EXEFS" "$EXTRACTED/exefs-plain"
  EXEFS="$EXTRACTED/exefs-plain"
  note "packing the RomFS straight from the container"
  "$SYS_PY" tools/nup_romfs_pack.py "$NUP" --package update --out "$EXTRACTED/romfs"
  ROMFS="$EXTRACTED/romfs"
fi

note "checking the ExeFS against $EXPECTED"
"$PY" - "$EXEFS" "$EXPECTED" <<'PY'
import hashlib, json, sys
from pathlib import Path
root, expected = Path(sys.argv[1]), Path(sys.argv[2])
want = json.loads(expected.read_text())['exefs_sha256']
bad = [n for n, sha in want.items() if hashlib.sha256((root / n).read_bytes()).hexdigest() != sha]
if bad:
    raise SystemExit(f'ExeFS mismatch in {root}: {", ".join(bad)} (wrong version or a damaged dump)')
print(f'  {len(want)} files match the 1.1.1 hashes')
PY

if [[ -f "$ROMFS/romfs.bin" && -f "$ROMFS/index.json" ]]; then
  note "using the packed RomFS at $ROMFS"
else
  [[ -d "$ROMFS" ]] || die "$ROMFS does not exist"
  note "packing $ROMFS"
  "$PY" tools/dkb_pack_romfs.py --dir "$ROMFS" --out "$EXTRACTED/romfs"
  ROMFS="$EXTRACTED/romfs"
fi

# ---------------------------------------------------------------- build
if [[ ! -x "$ROOT/build/xlate/totk_xlate" || $REBUILD_XLATE -eq 1 ]]; then
  note "building the shader translator"
  "$PY" tools/build_xlate.py
else
  note "shader translator already built (--rebuild-xlate rebuilds it)"
fi

if [[ $SKIP_LIFT -eq 0 ]]; then
  note "lifting main, subsdk0 and sdk to C"
  "$PY" recomp/gen.py --exefs "$EXEFS" --out "$GEN" --modules main,subsdk0,sdk \
    --patches recomp/patches-oot111.txt --jobs "$JOBS"
fi

[[ $SKIP_BUILD -eq 0 ]] || { note "stopping after the lift (--skip-build)"; exit 0; }

note "compiling and linking $OUT/oot"
"$PY" tools/build_game.py --version "$VERSION" --gen "$GEN" --out "$OUT" \
  --expected "$EXPECTED" --exefs "$EXEFS" --romfs "$ROMFS" --jobs "$JOBS"

note "done: $OUT/oot  (translator: build/xlate/totk_xlate)"
