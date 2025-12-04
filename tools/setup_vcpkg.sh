#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-${ROOT_DIR}/vcpkg}"

if [ ! -d "${VCPKG_ROOT}/.git" ]; then
  echo "[vcpkg] Cloning into ${VCPKG_ROOT}..."
  git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
fi

echo "[vcpkg] Bootstrapping..."
"${VCPKG_ROOT}/bootstrap-vcpkg.sh"

echo "[vcpkg] Ready at ${VCPKG_ROOT}"
