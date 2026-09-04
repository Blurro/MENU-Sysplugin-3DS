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

# HTTPSLIB transient version. Change this one value when bumping httpslib.
HTTPSLIB_VERSION=102

MAIN_BUILDER="${SCRIPT_DIR}/makeplugin.sh"
TRANSIENT_TOOL="${SCRIPT_DIR}/sysplugin/make_transient_3on.py"
COMPRESS_TOOL="${SCRIPT_DIR}/sysplugin/compress_3on_lzss.py"
HTTPSLIB_SOURCE="${SCRIPT_DIR}/sysmodules/rosalina/source/httpslib.c"
HTTPSLIB_BUILD="${SCRIPT_DIR}/sysmodules/rosalina/httpslib_tls/build.sh"
ONLINE_SOURCE="${SCRIPT_DIR}/sysmodules/rosalina/source/online.c"

for required in "$MAIN_BUILDER" "$TRANSIENT_TOOL" "$COMPRESS_TOOL" "$HTTPSLIB_SOURCE" "$HTTPSLIB_BUILD" "$ONLINE_SOURCE"; do
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
TEMP_HTTPS_OUT="${SCRIPT_DIR}/.httpslib.3on.new.$$"
TEMP_HTTPS_COMPRESSED="${SCRIPT_DIR}/.httpslib.3on.lz.new.$$"
TEMP_ONLINE_OUT="${SCRIPT_DIR}/.onlinetemporary.3on.new.$$"
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
    rm -f -- "$TEMP_BUILDER" "$TEMP_HTTPS_OUT" "$TEMP_HTTPS_COMPRESSED" "$TEMP_ONLINE_OUT"
    rm -f -- "${SCRIPT_DIR}/httpslib.998.3nx" "${SCRIPT_DIR}/onlinetemporary.999.3nx"
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

python3 - "$TEMP_BUILDER" <<'PY2'
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
    '"htps|httpslib|998|"',
    '"onln|onlinetemporary|999|"',
])
replace_array('LOADER_PLUGIN_CONFIG', [])
replace_array('METADATA_CONFIG', [])
replace_array('STACKED_PLUGIN_CONFIG', [])

text, count = re.subn(r'(?m)^STATE_DIR="\.plgbuild"$', 'STATE_DIR=".plgbuild-menuextras"', text, count=1)
if count != 1:
    raise SystemExit('ERROR: could not isolate MENU extras build state')
path.write_text(text)
PY2

printf '\n=========================================\n'
printf '======== Building MENU extras =========\n'
printf '=========================================\n\n'

seed_semantic_state
bash "$TEMP_BUILDER"
sync_semantic_state

HTTPS_NX="${SCRIPT_DIR}/httpslib.998.3nx"
ONLINE_NX="${SCRIPT_DIR}/onlinetemporary.999.3nx"
if [[ ! -f "$HTTPS_NX" || ! -f "$ONLINE_NX" ]]; then
    printf 'ERROR: private plugin build did not produce both transient source .3nx files.\n' >&2
    exit 1
fi

# Keep the stock builder's normal metadata path out of this temporary transient
# build. Pack httpslib's version metadata directly into its one-entry .3nx, then
# convert it to the versioned 3NX& form.
python3 - "$HTTPS_NX" "$HTTPSLIB_VERSION" <<'PY2'
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
try:
    version = int(sys.argv[2], 10)
except ValueError:
    raise SystemExit('ERROR: HTTPSLIB_VERSION must be a decimal integer')
if not 0 <= version <= 0xFFFFFFFF:
    raise SystemExit('ERROR: HTTPSLIB_VERSION must fit in an unsigned 32-bit value')
data = bytearray(path.read_bytes())
if len(data) < 0x30:
    raise SystemExit('ERROR: httpslib temporary .3nx is truncated')
header = list(struct.unpack_from('<12I', data, 0))
if header[0] != 0x24584E33 or data[4:8] != b'htps' or header[11] != 0:
    raise SystemExit('ERROR: httpslib temporary .3nx is not a clean single entry')
raw_end = 0x30 + header[5] + header[2] + header[3] + header[6]
body_end = (raw_end + 0xF) & ~0xF
if body_end != len(data):
    raise SystemExit('ERROR: httpslib temporary .3nx body layout is malformed')
# httpslib owns its version independently of MENU. Build the transient
# version metadata directly here, so no versionNNN.bin file is needed.
metadata = bytearray(b'3NXO' + struct.pack('<I', version))
metadata += b'\0' * (((len(metadata) + 0xF) & ~0xF) - len(metadata))
struct.pack_into('<I', data, 0x2C, len(metadata))
data += metadata
path.write_bytes(data)
PY2

python3 "$TRANSIENT_TOOL" "$HTTPS_NX" "$TEMP_HTTPS_OUT" htps
python3 "$TRANSIENT_TOOL" "$ONLINE_NX" "$TEMP_ONLINE_OUT" onln
python3 "$COMPRESS_TOOL" "$TEMP_HTTPS_OUT" "$TEMP_HTTPS_COMPRESSED"

python3 - "$TEMP_HTTPS_OUT" htps "$TEMP_ONLINE_OUT" onln <<'PY2'
import pathlib
import struct
import sys

TRANSIENT_MAGIC = 0x26584E33
for i in range(1, len(sys.argv), 2):
    path = pathlib.Path(sys.argv[i])
    expected_id = sys.argv[i + 1].encode('ascii')
    data = path.read_bytes()
    if len(data) < 0x2C:
        raise SystemExit(f'ERROR: transient output is truncated: {path}')
    if struct.unpack_from('<I', data, 0)[0] != TRANSIENT_MAGIC or data[4:8] != expected_id:
        raise SystemExit(f'ERROR: transient output validation failed: {path}')
PY2

mv -f -- "$TEMP_HTTPS_OUT" "${SCRIPT_DIR}/httpslib.3on"
mv -f -- "$TEMP_HTTPS_COMPRESSED" "${SCRIPT_DIR}/httpslib.3on.lz"
mv -f -- "$TEMP_ONLINE_OUT" "${SCRIPT_DIR}/onlinetemporary.3on"
rm -f -- "$HTTPS_NX" "$ONLINE_NX"

printf '\nWrote httpslib.3on (%s bytes, version %s, versioned 3NX& transient)\n' "$(stat -c '%s' -- "${SCRIPT_DIR}/httpslib.3on")" "$HTTPSLIB_VERSION"
printf 'Wrote httpslib.3on.lz (%s bytes, embedded MENU fallback)\n' "$(stat -c '%s' -- "${SCRIPT_DIR}/httpslib.3on.lz")"
printf 'Wrote onlinetemporary.3on (%s bytes, source-neutral 3NX& transient)\n' "$(stat -c '%s' -- "${SCRIPT_DIR}/onlinetemporary.3on")"
printf '\nDone. Normal plugin configuration remains in stock makeplugin.sh.\n\n'
