#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${PROJECT_ROOT}"

echo "INFO: Running clang-format..."

find include src tests -type f \
    \( -name "*.cpp" -o -name "*.hpp" \) \
    -exec clang-format -i {} +

echo "INFO: Formatting complete."
