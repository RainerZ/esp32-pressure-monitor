#!/usr/bin/env bash
#
# Refresh the vendored XCPlite subset in xcplite/ from an XCPlite repository.
#
# The ESP32 firmware only needs the FreeRTOS/RTOS-configuration subset of
# XCPlite, so this script clones the upstream repository into a temporary
# directory, copies exactly the files listed in the manifest below, and records
# the origin and commit in xcplite/VERSION.
#
# Usage:
#   tools/update_xcplite.sh                          # reuse repo+ref from xcplite/VERSION
#   tools/update_xcplite.sh --ref V2.1.10
#   tools/update_xcplite.sh --repo https://github.com/RainerZ/XCPlite --ref master
#   tools/update_xcplite.sh --repo ~/git/XCPlite-RainerZ --ref esp32_pressure_monitor
#
# A local path is a valid --repo. That is currently required, because the
# XCPlite library changes this project depends on only exist on a local branch.

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="${PROJECT_DIR}/xcplite"
VERSION_FILE="${VENDOR_DIR}/VERSION"

DEFAULT_REPO="https://github.com/RainerZ/XCPlite"
DEFAULT_REF="master"

# Public API headers, copied to xcplite/inc/
INC_FILES=(
    a2l.h
    a2l.hpp
    xcplib.h
    xcplib.hpp
)

# Library sources built by extra_script.py, plus the internal headers they need.
# Keep the .c list in sync with XCPLITE_SOURCES in extra_script.py.
SRC_FILES=(
    cal.c
    platform.c
    queue32m.c
    xcpappl.c
    xcpethserver.c
    xcpethtl.c
    xcplite.c

    cal.h
    dbg_print.h
    persistence.h
    platform.h
    queue.h
    shm.h
    xcp.h
    xcp_cfg.h
    xcpethserver.h
    xcpethtl.h
    xcplib_cfg.h
    xcplib_rtos_cfg.h
    xcplite.h
    xcptl.h
    xcptl_cfg.h
)

# Extra files copied to xcplite/ itself
ROOT_FILES=(
    LICENSE
)

repo=""
ref=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo)
            repo="$2"
            shift 2
            ;;
        --ref)
            ref="$2"
            shift 2
            ;;
        -h|--help)
            sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

# Fall back to whatever the current snapshot was taken from
if [[ -z "${repo}" && -f "${VERSION_FILE}" ]]; then
    repo="$(sed -n 's/^repo: //p' "${VERSION_FILE}" | head -1)"
fi
if [[ -z "${ref}" && -f "${VERSION_FILE}" ]]; then
    ref="$(sed -n 's/^ref: //p' "${VERSION_FILE}" | head -1)"
fi
repo="${repo:-${DEFAULT_REPO}}"
ref="${ref:-${DEFAULT_REF}}"

# Expand a local path so `git clone` and the VERSION file agree
if [[ -d "${repo}" ]]; then
    repo="$(cd "${repo}" && pwd)"
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/xcplite-update.XXXXXX")"
trap 'rm -rf "${tmp_dir}"' EXIT

echo "Cloning ${repo} (${ref})..."
git clone --quiet --depth 1 --branch "${ref}" "${repo}" "${tmp_dir}/XCPlite" 2>/dev/null ||
    git clone --quiet --branch "${ref}" "${repo}" "${tmp_dir}/XCPlite"

clone="${tmp_dir}/XCPlite"
commit="$(git -C "${clone}" rev-parse HEAD)"
commit_date="$(git -C "${clone}" show -s --format=%cI HEAD)"
subject="$(git -C "${clone}" show -s --format=%s HEAD)"

missing=0
for file in "${INC_FILES[@]}"; do
    [[ -f "${clone}/inc/${file}" ]] || { echo "Missing inc/${file} in ${ref}" >&2; missing=1; }
done
for file in "${SRC_FILES[@]}"; do
    [[ -f "${clone}/src/${file}" ]] || { echo "Missing src/${file} in ${ref}" >&2; missing=1; }
done
[[ ${missing} -eq 0 ]] || { echo "Aborting, xcplite/ left unchanged." >&2; exit 1; }

# Only remove what this script owns, so VERSION history stays comprehensible
rm -rf "${VENDOR_DIR}/inc" "${VENDOR_DIR}/src"
mkdir -p "${VENDOR_DIR}/inc" "${VENDOR_DIR}/src"

for file in "${INC_FILES[@]}"; do
    cp "${clone}/inc/${file}" "${VENDOR_DIR}/inc/${file}"
done
for file in "${SRC_FILES[@]}"; do
    cp "${clone}/src/${file}" "${VENDOR_DIR}/src/${file}"
done
for file in "${ROOT_FILES[@]}"; do
    [[ -f "${clone}/${file}" ]] && cp "${clone}/${file}" "${VENDOR_DIR}/${file}"
done

cat > "${VERSION_FILE}" <<EOF
# Vendored XCPlite snapshot. Regenerate with tools/update_xcplite.sh.
repo: ${repo}
ref: ${ref}
commit: ${commit}
date: ${commit_date}
subject: ${subject}
EOF

echo "Updated ${VENDOR_DIR} from ${ref} @ ${commit:0:12} (${subject})"
echo "Files: $(( ${#INC_FILES[@]} )) headers in inc/, $(( ${#SRC_FILES[@]} )) files in src/"
