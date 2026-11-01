#!/bin/bash
#
# test_package_relocatable.sh - CR-006 §5/§9 unit test.
# Installs the SDK component to an isolated prefix and verifies the generated
# CMake package files (Config/ConfigVersion/Targets*) are relocatable and free
# of host/personal/build-tree/source-tree/third_party absolute paths.
#
# The pkg-config .pc files legitimately carry the configured install prefix by
# design (standard pkg-config convention); the relocation gate applies to the
# CMake Config/Targets, which are the consumption path for `find_package`.
#
# Args: $1 = build dir, $2 = source dir
#
set -euo pipefail

BUILD_DIR="${1:?build dir required}"
SRC_DIR="${2:?source dir required}"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

SDK_PREFIX="${TMP}/sdk"
cmake --install "${BUILD_DIR}" --component ccu-framework-sdk --prefix "${SDK_PREFIX}" >/dev/null

CMAKE_DIR="${SDK_PREFIX}/lib/cmake/CCUFramework"
if [[ ! -d "${CMAKE_DIR}" ]]; then
    echo "test_package_relocatable: FAIL missing ${CMAKE_DIR}"; exit 1
fi

# Forbidden absolute path tokens that must never appear in Config/Targets.
FORBIDDEN=(
    "${SRC_DIR}"                       # source tree
    "${BUILD_DIR}"                     # build tree
    "third_party"                      # vendored host archive path
    "/usr/local"                       # host /usr/local
    "/opt/homebrew"                    # Homebrew
    "${HOME}"                          # personal/home directory
)

fail=0
shopt -s nullglob
files=( "${CMAKE_DIR}"/CCUFrameworkConfig.cmake
        "${CMAKE_DIR}"/CCUFrameworkConfigVersion.cmake
        "${CMAKE_DIR}"/CCUFrameworkTargets.cmake
        "${CMAKE_DIR}"/CCUFrameworkTargets-*.cmake )
shopt -u nullglob

if [[ ${#files[@]} -eq 0 ]]; then
    echo "test_package_relocatable: FAIL no CMake package files found"; exit 1
fi

for f in "${files[@]}"; do
    for bad in "${FORBIDDEN[@]}"; do
        if grep -F -n -- "${bad}" "${f}" >/dev/null 2>&1; then
            echo "test_package_relocatable: FAIL $(basename "${f}") references forbidden path '${bad}':"
            grep -F -n -- "${bad}" "${f}" | sed 's/^/    /'
            fail=1
        fi
    done
done

# Relocatability: the Config must derive its prefix from its own location
# (@PACKAGE_INIT@ -> PACKAGE_PREFIX_DIR), not bake an absolute prefix.
config="${CMAKE_DIR}/CCUFrameworkConfig.cmake"
if ! grep -F -q 'PACKAGE_PREFIX_DIR' "${config}" 2>/dev/null \
   && ! grep -F -q 'get_filename_component' "${config}" 2>/dev/null; then
    echo "test_package_relocatable: FAIL Config is not relocatable (no PACKAGE_PREFIX_DIR derivation)"
    fail=1
fi

if [[ ${fail} -ne 0 ]]; then
    echo "test_package_relocatable: FAIL"; exit 1
fi
echo "test_package_relocatable: PASS"
