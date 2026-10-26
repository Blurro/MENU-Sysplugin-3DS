#!/bin/bash
set -euo pipefail

if [[ -n "${DEVKITARM:-}" && -d "${DEVKITARM}/bin" ]]; then
    export PATH="${DEVKITARM}/bin:${PATH}"
elif [[ -n "${DEVKITPRO:-}" && -d "${DEVKITPRO}/devkitARM/bin" ]]; then
    export PATH="${DEVKITPRO}/devkitARM/bin:${PATH}"
elif [[ -d "/opt/devkitpro/devkitARM/bin" ]]; then
    export PATH="/opt/devkitpro/devkitARM/bin:${PATH}"
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$SCRIPT_DIR"

BUILD_PATCH="${SCRIPT_DIR}/sysmodules/rosalina/httpslib_tls/rosalina-makefile.patch"

python3 - "$SCRIPT_DIR" "$BUILD_PATCH" <<'PY_BUILD'
from pathlib import Path
import hashlib
import subprocess
import sys

root = Path(sys.argv[1])
patch = Path(sys.argv[2])
makefile = root / 'sysmodules/rosalina/Makefile'
stock = '7358f39a996480c2cdb21dbb0ec2fdc57a71f2fde7e9a84158a008eaff840bab'
prepared = '65c3378d08a2826b0843a239726c87dab416bc9d6b8f5e753702978f597ec949'
digest = hashlib.sha256(makefile.read_bytes()).hexdigest()
if digest == stock:
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch)], cwd=root, check=True)
elif digest != prepared:
    raise SystemExit('ERROR: unsupported Rosalina Makefile; use the pinned clean Nexus source.')
if hashlib.sha256(makefile.read_bytes()).hexdigest() != prepared:
    raise SystemExit('ERROR: MENU TLS build integration did not match the expected result.')
PY_BUILD

# HTTPSLIB transient version. Change this one value when bumping httpslib.
HTTPSLIB_VERSION=111

MAIN_BUILDER="${SCRIPT_DIR}/makeplugin.sh"
FINALIZE_TOOL="${SCRIPT_DIR}/sysplugin/finalize_transient_3on.py"
COMPRESS_TOOL="${SCRIPT_DIR}/sysplugin/compress_lzss.py"
HTTPSLIB_SOURCE="${SCRIPT_DIR}/sysmodules/rosalina/source/httpslib.c"
HTTPSLIB_SOURCES="${SCRIPT_DIR}/sysmodules/rosalina/source/httpslib_sources.c"
HTTPSLIB_BUILD="${SCRIPT_DIR}/sysmodules/rosalina/httpslib_tls/build.sh"
ONLINE_SOURCE="${SCRIPT_DIR}/sysmodules/rosalina/source/online.c"

for required in "$MAIN_BUILDER" "$FINALIZE_TOOL" "$COMPRESS_TOOL" "$HTTPSLIB_SOURCE" "$HTTPSLIB_SOURCES" "$HTTPSLIB_BUILD" "$ONLINE_SOURCE"; do
    if [[ ! -f "$required" ]]; then
        printf 'ERROR: required MENU extras file is missing: %s\n' "$required" >&2
        exit 1
    fi
done

if ! grep -q 'PLUGIN_MAIN(htps)' "$HTTPSLIB_SOURCE" || ! grep -q 'PLUGIN_MAIN(onln)' "$ONLINE_SOURCE"; then
    printf 'ERROR: transient source entry point missing.\n' >&2
    exit 1
fi
if ! grep -q 'MODULE_RELINK_REQUIRED' "$MAIN_BUILDER" || ! grep -q 'marker-sources.tsv' "$MAIN_BUILDER"; then
    printf 'ERROR: makeplugin.sh is not the selective-object-invalidation dev-kit builder.\n' >&2
    exit 1
fi

TEMP_BUILDER="$(mktemp "${SCRIPT_DIR}/.makemenuextras.makeplugin.XXXXXX")"
BACKUP_DIR="$(mktemp -d "${SCRIPT_DIR}/.makemenuextras.backup.XXXXXX")"
TEMP_HTTPS_COMPRESSED="${SCRIPT_DIR}/.httpslib.3on.lz.new.$$"
TEMP_HTTPS_LZ10="${SCRIPT_DIR}/.httpslib.lz10.new.$$"
VERSION_METADATA="${SCRIPT_DIR}/.httpslib-version.$$"
EXTRA_STATE_DIR="${SCRIPT_DIR}/.plgbuild-menuextras"
SEMANTIC_STATE_DIR="${SCRIPT_DIR}/.plgbuild"
mkdir -p "$EXTRA_STATE_DIR" "$SEMANTIC_STATE_DIR"

semantic_state_files() {
    local module_name="$1"
    printf '%s\n' \
        "${module_name}.markers.sha256" \
        "${module_name}.marker-sources.tsv" \
        "${module_name}.marker-compiler.sha256"
}

seed_semantic_state() {
    local module_name file
    for module_name in rosalina loader; do
        while IFS= read -r file; do
            [[ -f "${SEMANTIC_STATE_DIR}/${file}" ]] || continue
            cp -f -- "${SEMANTIC_STATE_DIR}/${file}" "${EXTRA_STATE_DIR}/${file}"
        done < <(semantic_state_files "$module_name")
    done
}

sync_semantic_state() {
    local module_name file
    for module_name in rosalina loader; do
        while IFS= read -r file; do
            [[ -f "${EXTRA_STATE_DIR}/${file}" ]] || continue
            cp -f -- "${EXTRA_STATE_DIR}/${file}" "${SEMANTIC_STATE_DIR}/${file}"
        done < <(semantic_state_files "$module_name")
    done
}

GENERATED_FILES=(
    "sysmodules/rosalina/3dsx.ld"
    "sysmodules/rosalina/create3nx.py"
    "sysmodules/rosalina/plgmarkers.ld"
    "sysmodules/rosalina/rosalina.elf"
)

: > "${BACKUP_DIR}/existing.list"
for rel in "${GENERATED_FILES[@]}"; do
    if [[ -e "${SCRIPT_DIR}/${rel}" || -L "${SCRIPT_DIR}/${rel}" ]]; then
        mkdir -p "${BACKUP_DIR}/$(dirname -- "$rel")"
        cp -a -- "${SCRIPT_DIR}/${rel}" "${BACKUP_DIR}/${rel}"
        printf '%s\n' "$rel" >> "${BACKUP_DIR}/existing.list"
    fi
done

cleanup() {
    local status=$?
    trap - EXIT
    set +e
    rm -f -- "$TEMP_BUILDER" "$TEMP_HTTPS_COMPRESSED" "$TEMP_HTTPS_LZ10" "$VERSION_METADATA"
    rm -f -- "${SCRIPT_DIR}/httpslib-build.3on" "${SCRIPT_DIR}/onlinepagetemp-build.3on"
    for rel in "${GENERATED_FILES[@]}"; do
        rm -f -- "${SCRIPT_DIR}/${rel}"
    done
    while IFS= read -r rel; do
        [[ -n "$rel" ]] || continue
        mkdir -p "${SCRIPT_DIR}/$(dirname -- "$rel")"
        cp -a -- "${BACKUP_DIR}/${rel}" "${SCRIPT_DIR}/${rel}"
    done < "${BACKUP_DIR}/existing.list"
    rm -rf -- "$BACKUP_DIR"
    exit "$status"
}
trap cleanup EXIT INT TERM HUP

cp -- "$MAIN_BUILDER" "$TEMP_BUILDER"
chmod +x "$TEMP_BUILDER"

python3 - "$TEMP_BUILDER" "${VERSION_METADATA##*/}" <<'PY2'
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()

def replace_array(name, body):
    global text
    pattern = re.compile(rf'(?ms)^{re.escape(name)}=\(\n.*?^\)\n')
    replacement = name + '=(\n' + ''.join(f'    {line}\n' for line in body) + ')\n'
    text, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise SystemExit(f'ERROR: could not locate {name} in makeplugin.sh')

replace_array('ROSALINA_PLUGIN_CONFIG', [
    '"htps|httpslib-build||transient"',
    '"onln|onlinepagetemp-build||transient"',
])
replace_array('LOADER_PLUGIN_CONFIG', [])
replace_array('METADATA_CONFIG', [f'"httpslib-build|{sys.argv[2]}"'])
replace_array('STACKED_PLUGIN_CONFIG', [])

text, count = re.subn(r'(?m)^STATE_DIR="\.plgbuild"$', 'STATE_DIR=".plgbuild-menuextras"', text, count=1)
if count != 1:
    raise SystemExit('ERROR: could not isolate MENU extras build state')
path.write_text(text)
PY2

python3 - "$VERSION_METADATA" "$HTTPSLIB_VERSION" <<'PY2'
import pathlib
import struct
import sys

try:
    version = int(sys.argv[2], 10)
except ValueError:
    raise SystemExit('ERROR: HTTPSLIB_VERSION must be a decimal integer')
if not 0 <= version <= 0xFFFFFFFF:
    raise SystemExit('ERROR: HTTPSLIB_VERSION must fit in an unsigned 32-bit value')
pathlib.Path(sys.argv[1]).write_bytes(b'3NXO' + struct.pack('<I', version))
PY2

printf '\n=========================================\n'
printf '======== Building MENU extras =========\n'
printf '=========================================\n\n'

# remove leftovers
rm -f -- \
    "${SCRIPT_DIR}/sysmodules/rosalina/build/httpslib.o" \
    "${SCRIPT_DIR}/sysmodules/rosalina/build/httpslib.d" \
    "${SCRIPT_DIR}/sysmodules/rosalina/build/online.o" \
    "${SCRIPT_DIR}/sysmodules/rosalina/build/online.d" \
    "${SCRIPT_DIR}/sysmodules/rosalina/build/modmenu.o" \
    "${SCRIPT_DIR}/sysmodules/rosalina/build/modmenu.d"

seed_semantic_state
bash "$TEMP_BUILDER"
sync_semantic_state

HTTPS_ON="${SCRIPT_DIR}/httpslib-build.3on"
ONLINE_ON="${SCRIPT_DIR}/onlinepagetemp-build.3on"
if [[ ! -f "$HTTPS_ON" || ! -f "$ONLINE_ON" ]]; then
    printf 'ERROR: private plugin build did not produce both .3on files.\n' >&2
    exit 1
fi

python3 "$COMPRESS_TOOL" "$HTTPS_ON" "$TEMP_HTTPS_LZ10"

# The MENU HTTPS fallback stores a version and length before the raw LZ10 stream.
python3 - "$HTTPS_ON" "$TEMP_HTTPS_LZ10" "$TEMP_HTTPS_COMPRESSED" "$HTTPSLIB_VERSION" <<'PY2'
from pathlib import Path
import struct
import sys

source = Path(sys.argv[1]).read_bytes()
if len(source) < 0x30:
    raise SystemExit('ERROR: HTTPS transient has no modern metadata header')
fields = struct.unpack_from('<12I', source)
magic, _pid, code_size, data_size, _bss_size, reloc_size, repair_size = fields[:7]
metadata_size = fields[11]
if magic != 0x26584E33:
    raise SystemExit('ERROR: HTTPS transient is not a 3NX& transient')
raw_end = 0x30 + reloc_size + code_size + data_size + repair_size
metadata_offset = (raw_end + 15) & ~15
if metadata_size < 8 or metadata_offset + metadata_size != len(source):
    raise SystemExit('ERROR: HTTPS transient metadata is missing or malformed')
if source[metadata_offset:metadata_offset + 4] != b'3NXO':
    raise SystemExit('ERROR: HTTPS transient metadata does not begin with 3NXO')
version = struct.unpack_from('<I', source, metadata_offset + 4)[0]
if version != int(sys.argv[4], 10):
    raise SystemExit('ERROR: HTTPS transient metadata version does not match HTTPSLIB_VERSION')

compressed = Path(sys.argv[2]).read_bytes()
if len(compressed) < 4 or compressed[0] != 0x10:
    raise SystemExit('ERROR: compressed HTTPS transient has no LZ10 header')
size = compressed[1] | (compressed[2] << 8) | (compressed[3] << 16)
if size != len(source):
    raise SystemExit('ERROR: compressed HTTPS transient has the wrong size')
Path(sys.argv[3]).write_bytes(b'3NXO' + struct.pack('<II', version, len(compressed)) + compressed)
PY2

mv -f -- "$HTTPS_ON" "${SCRIPT_DIR}/httpslib.3on"
mv -f -- "$TEMP_HTTPS_COMPRESSED" "${SCRIPT_DIR}/httpslib.3on.lz"
mv -f -- "$ONLINE_ON" "${SCRIPT_DIR}/onlinepagetemp.3on"

printf '\nWrote httpslib.3on (%s bytes, version %s, versioned 3NX& transient)\n' "$(stat -c '%s' -- "${SCRIPT_DIR}/httpslib.3on")" "$HTTPSLIB_VERSION"
printf 'Wrote httpslib.3on.lz (%s bytes, embedded MENU fallback)\n' "$(stat -c '%s' -- "${SCRIPT_DIR}/httpslib.3on.lz")"
printf 'Wrote onlinepagetemp.3on (%s bytes, source-neutral 3NX& transient)\n' "$(stat -c '%s' -- "${SCRIPT_DIR}/onlinepagetemp.3on")"
printf '\nDone. Normal plugin configuration remains in stock makeplugin.sh.\n\n'
