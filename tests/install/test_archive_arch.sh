#!/bin/bash
#
# test_archive_arch.sh - CR-006 §9 unit test.
# Verifies the five framework static archives exist and that every object
# member shares one consistent architecture matching the build platform, with
# no foreign-format (Mach-O/x86_64 on Linux, ELF on macOS) pollution.
#
# Args: $1 = build dir, $2 = source dir
#
set -euo pipefail

BUILD_DIR="${1:?build dir required}"
SRC_DIR="${2:?source dir required}"
# Resolve to absolute paths: later we cd into a temp extraction dir.
BUILD_DIR="$(cd "${BUILD_DIR}" && pwd)"
SRC_DIR="$(cd "${SRC_DIR}" && pwd)"

ARCHIVES=(
    "${BUILD_DIR}/libcgw-framework-config.a"
    "${BUILD_DIR}/libcgw-framework-log.a"
    "${BUILD_DIR}/libcgw-framework-store.a"
    "${BUILD_DIR}/libcgw-framework-hash.a"
    "${BUILD_DIR}/libcgw-framework-someip.a"
)

# Expected member architecture token derived from the host platform.
detect_expected() {
    case "$(uname -s)" in
        Darwin)
            case "$(uname -m)" in
                arm64|aarch64) echo "arm64" ;;
                x86_64)        echo "x86_64" ;;
            esac ;;
        Linux)
            case "$(uname -m)" in
                aarch64|arm64) echo "aarch64" ;;
                x86_64)        echo "x86_64" ;;
            esac ;;
    esac
}
EXPECTED="$(detect_expected)"
if [[ -z "${EXPECTED}" ]]; then
    echo "test_archive_arch: SKIP (unsupported host: $(uname -s) $(uname -m))"; exit 0
fi

# Forbidden tokens: formats/architectures that must never appear.
forbidden_for_host() {
    case "$(uname -s)" in
        Darwin) printf '%s\n' "ELF" ;;
        Linux)  printf '%s\n' "Mach-O" ;;
    esac
}

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

fail=0
for arc in "${ARCHIVES[@]}"; do
    name="$(basename "${arc}")"
    if [[ ! -f "${arc}" ]]; then
        echo "test_archive_arch: FAIL ${name} missing (${arc})"; fail=1; continue
    fi

    # Extract every member into a temp dir and inspect with `file`.
    member_dir="${TMP}/${name%.a}"
    mkdir -p "${member_dir}"; (cd "${member_dir}" && ar x "${arc}")

    members_found=0
    while IFS= read -r -d '' obj; do
        info="$(file "${obj}")"
        # Skip archive index/symbol-table members (e.g. __.SYMDEF): they are
        # not object code and have no meaningful architecture.
        case "${info}" in
            *Mach-O*|*ELF*) ;;  # object code: inspect below
            *) continue ;;      # data/index member: skip
        esac
        members_found=$((members_found + 1))

        # Architecture check: the expected token must appear.
        if [[ "${info}" != *"${EXPECTED}"* ]]; then
            echo "test_archive_arch: FAIL ${name} member $(basename "${obj}") arch mismatch: ${info}"; fail=1
        fi

        # Pollution check: forbidden format tokens must not appear.
        while IFS= read -r bad; do
            if [[ -n "${bad}" && "${info}" == *"${bad}"* ]]; then
                echo "test_archive_arch: FAIL ${name} member $(basename "${obj}") foreign format '${bad}': ${info}"; fail=1
            fi
        done < <(forbidden_for_host)
    done < <(find "${member_dir}" -type f -print0)

    if [[ ${members_found} -eq 0 ]]; then
        echo "test_archive_arch: FAIL ${name} has no object members"; fail=1
    else
        echo "test_archive_arch: ok ${name} (${members_found} members, ${EXPECTED})"
    fi
done

if [[ ${fail} -ne 0 ]]; then
    echo "test_archive_arch: FAIL"; exit 1
fi
echo "test_archive_arch: PASS"
