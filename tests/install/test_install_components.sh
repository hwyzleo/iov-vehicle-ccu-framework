#!/bin/bash
#
# test_install_components.sh - CR-006 §4/§9 unit test.
# Installs the ccu-framework-sdk and ccu-framework-runtime components from the
# build tree to isolated prefixes and verifies:
#   - SDK contains the 5 archives, public headers, Config/Version/Targets and
#     the 5 per-component .pc files.
#   - Runtime installs an explicit empty set (no placeholder files).
#   - No formal file lands in the CMake default "Unspecified" component: the
#     union of SDK + runtime equals the full (unfiltered) install.
#
# Args: $1 = build dir
#
set -euo pipefail

BUILD_DIR="${1:?build dir required}"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

SDK_PREFIX="${TMP}/sdk"
RT_PREFIX="${TMP}/runtime"
ALL_PREFIX="${TMP}/all"

# Install each component to its own isolated prefix.
cmake --install "${BUILD_DIR}" --component ccu-framework-sdk   --prefix "${SDK_PREFIX}" >/dev/null
cmake --install "${BUILD_DIR}" --component ccu-framework-runtime --prefix "${RT_PREFIX}" >/dev/null
cmake --install "${BUILD_DIR}" --prefix "${ALL_PREFIX}" >/dev/null

fail=0

# --- SDK file set ---
sdk_libs=(config log store hash someip)
for c in "${sdk_libs[@]}"; do
    if [[ ! -f "${SDK_PREFIX}/lib/libccu-framework-${c}.a" ]]; then
        echo "test_install_components: FAIL SDK missing libccu-framework-${c}.a"; fail=1
    fi
    if [[ ! -f "${SDK_PREFIX}/lib/pkgconfig/ccu-framework-${c}.pc" ]]; then
        echo "test_install_components: FAIL SDK missing ccu-framework-${c}.pc"; fail=1
    fi
done

for f in \
    "lib/cmake/CCUFramework/CCUFrameworkConfig.cmake" \
    "lib/cmake/CCUFramework/CCUFrameworkConfigVersion.cmake" \
    "lib/cmake/CCUFramework/CCUFrameworkTargets.cmake"
do
    if [[ ! -f "${SDK_PREFIX}/${f}" ]]; then
        echo "test_install_components: FAIL SDK missing ${f}"; fail=1
    fi
done

# Public headers: the flat headers + the ccu/fw subtree.
for h in config.h config_types.h log.h log_types.h store.h store_types.h; do
    if [[ ! -f "${SDK_PREFIX}/include/${h}" ]]; then
        echo "test_install_components: FAIL SDK missing include/${h}"; fail=1
    fi
done
for h in \
    "include/ccu/fw/hash/sha256.hpp" \
    "include/ccu/fw/hash/hex.hpp" \
    "include/ccu/fw/someip/runtime.hpp" \
    "include/ccu/fw/someip/provider.hpp" \
    "include/ccu/fw/someip/client.hpp" \
    "include/ccu/fw/someip/types.hpp"
do
    if [[ ! -f "${SDK_PREFIX}/${h}" ]]; then
        echo "test_install_components: FAIL SDK missing ${h}"; fail=1
    fi
done

# --- Runtime: explicit empty set ---
rt_count=0
if [[ -d "${RT_PREFIX}" ]]; then
    rt_count=$( { find "${RT_PREFIX}" -type f 2>/dev/null || true; } | wc -l | tr -d ' ')
fi
if [[ "${rt_count}" -ne 0 ]]; then
    echo "test_install_components: FAIL runtime component must be empty, found ${rt_count} files:"; fail=1
    find "${RT_PREFIX}" -type f | sed 's/^/    /'
else
    echo "test_install_components: ok runtime is an explicit empty set"
fi

# --- No Unspecified component: SDK ∪ runtime == full install ---
( cd "${SDK_PREFIX}" 2>/dev/null && find . -type f | sort ) > "${TMP}/sdk.list" || true
( cd "${RT_PREFIX}"  2>/dev/null && find . -type f | sort ) > "${TMP}/rt.list"  || true
( cd "${ALL_PREFIX}" 2>/dev/null && find . -type f | sort ) > "${TMP}/all.list" || true
cat "${TMP}/sdk.list" "${TMP}/rt.list" | sort -u > "${TMP}/union.list"

unspecified="$(comm -23 "${TMP}/all.list" "${TMP}/union.list")"
if [[ -n "${unspecified}" ]]; then
    echo "test_install_components: FAIL files in Unspecified component (not SDK/runtime):"
    echo "${unspecified}" | sed 's/^/    /'
    fail=1
else
    echo "test_install_components: ok no Unspecified component files"
fi

if [[ ${fail} -ne 0 ]]; then
    echo "test_install_components: FAIL"; exit 1
fi
echo "test_install_components: PASS"
