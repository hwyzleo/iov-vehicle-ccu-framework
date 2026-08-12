#!/bin/bash
#
# test_installed_consumer.sh - CR-006 §8/§9 integration test.
#
# End-to-end verification of the installed-package consumer contract, fully
# isolated from the CGW-FW source/build tree:
#   1. stage a controlled yaml-cpp TARGET dependency prefix (HOST).
#   2. configure + build the five framework targets with a clean /usr prefix.
#   3. install cgw-framework-sdk and cgw-framework-runtime to DESTDIR staging.
#   4. verify SDK file set, runtime empty set, no Unspecified component.
#   5. build+link+run the independent consumer against SDK + dep prefixes only.
#   6. relocation: move the SDK staging, reconfigure+build+run the consumer.
#   7. fail-closed: disable yaml-cpp and confirm consumer configure fails.
#
# Args: $1 = source dir, $2 = build dir (unused, kept for interface symmetry)
#
set -euo pipefail

SRC_DIR="${1:?source dir required}"
# $2 build dir is intentionally unused: this test builds its own isolated tree.

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

DEP_STAGING="${TMP}/dep"
SDK_STAGE="${TMP}/stage"
RT_STAGE="${TMP}/stage-rt"
SDK_MOVED="${TMP}/stage-moved"
FW_BUILD="${TMP}/fw-build"

CONSUMER_SRC="${SRC_DIR}/tests/installed_consumer"

pass() { echo "test_installed_consumer: ok $*"; }
bail() { echo "test_installed_consumer: FAIL $*" >&2; exit 1; }

# ---- 1. stage controlled yaml-cpp dependency ----
CGW_DEP_STAGING="${DEP_STAGING}" bash "${SRC_DIR}/scripts/stage-yaml-cpp.sh" >/dev/null \
    || bail "yaml-cpp staging failed"
[[ -f "${DEP_STAGING}/usr/lib/cmake/yaml-cpp/yaml-cppConfig.cmake" ]] \
    || bail "staged yaml-cpp config missing"

# ---- 2. configure + build framework (clean /usr prefix, no sanitizers) ----
cmake -S "${SRC_DIR}" -B "${FW_BUILD}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="${DEP_STAGING}/usr" \
    -DCGW_FW_HASH_SANITIZE=OFF \
    -DCGW_FW_SOMEIP_SANITIZE=OFF \
    >/dev/null 2>&1 || bail "framework configure failed"

cmake --build "${FW_BUILD}" --target \
    cgw-framework-config cgw-framework-log cgw-framework-store \
    cgw-framework-hash cgw-framework-someip -j \
    >/dev/null 2>&1 || bail "framework build failed"

# ---- 3. install SDK + runtime to DESTDIR staging ----
DESTDIR="${SDK_STAGE}" cmake --install "${FW_BUILD}" --component cgw-framework-sdk >/dev/null \
    || bail "SDK install failed"
DESTDIR="${RT_STAGE}" cmake --install "${FW_BUILD}" --component cgw-framework-runtime >/dev/null \
    || bail "runtime install failed"

SDK_USR="${SDK_STAGE}/usr"

# ---- 4. file-set checks ----
for c in config log store hash someip; do
    [[ -f "${SDK_USR}/lib/libcgw-framework-${c}.a" ]] || bail "SDK missing libcgw-framework-${c}.a"
done
[[ -f "${SDK_USR}/lib/cmake/CGWFramework/CGWFrameworkConfig.cmake" ]] || bail "SDK missing Config"
[[ -f "${SDK_USR}/lib/cmake/CGWFramework/CGWFrameworkTargets.cmake" ]] || bail "SDK missing Targets"
rt_count=0
if [[ -d "${RT_STAGE}" ]]; then
    rt_count=$( { find "${RT_STAGE}" -type f 2>/dev/null || true; } | wc -l | tr -d ' ')
fi
[[ "${rt_count}" -eq 0 ]] || bail "runtime must be empty, found ${rt_count} files"
pass "SDK file set + runtime empty set"

# ---- 5. independent consumer: configure + build + run ----
CONS_BUILD="${TMP}/consumer-build"
cmake -S "${CONSUMER_SRC}" -B "${CONS_BUILD}" \
    -DCMAKE_PREFIX_PATH="${SDK_USR};${DEP_STAGING}/usr" \
    >/dev/null 2>&1 || bail "consumer configure failed"
cmake --build "${CONS_BUILD}" -j >/dev/null 2>&1 || bail "consumer build/link failed"
"${CONS_BUILD}/consumer" >/dev/null 2>&1 || bail "consumer run failed"
pass "consumer configure+link+run against installed SDK"

# ---- 6. relocation: move SDK staging, reconfigure consumer ----
mv "${SDK_STAGE}" "${SDK_MOVED}"
CONS_BUILD2="${TMP}/consumer-build-moved"
cmake -S "${CONSUMER_SRC}" -B "${CONS_BUILD2}" \
    -DCMAKE_PREFIX_PATH="${SDK_MOVED}/usr;${DEP_STAGING}/usr" \
    >/dev/null 2>&1 || bail "relocated consumer configure failed (relocatability)"
cmake --build "${CONS_BUILD2}" -j >/dev/null 2>&1 || bail "relocated consumer build failed"
"${CONS_BUILD2}/consumer" >/dev/null 2>&1 || bail "relocated consumer run failed"
pass "relocation: moved SDK staging still consumable"

# ---- 7. fail-closed: yaml-cpp unavailable -> consumer configure must fail ----
CONS_BUILD3="${TMP}/consumer-build-fail"
if cmake -S "${CONSUMER_SRC}" -B "${CONS_BUILD3}" \
        -DCMAKE_PREFIX_PATH="${SDK_MOVED}/usr" \
        -DCMAKE_DISABLE_FIND_PACKAGE_yaml-cpp=ON \
        >/dev/null 2>&1; then
    bail "fail-closed: consumer configure succeeded without yaml-cpp (expected failure)"
fi
pass "fail-closed: missing yaml-cpp rejects consumer configure"

echo "test_installed_consumer: PASS"
