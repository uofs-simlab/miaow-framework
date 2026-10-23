#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

clang_format="${CLANG_FORMAT:-clang-format}"
mode="format"

if [[ "${1:-}" == "--check" ]]; then
    mode="check"
elif [[ "$#" -gt 0 ]]; then
    echo "usage: scripts/format.sh [--check]" >&2
    exit 2
fi

if ! command -v "${clang_format}" >/dev/null 2>&1; then
    echo "clang-format not found: ${clang_format}" >&2
    exit 1
fi

files=()
while IFS= read -r -d '' file; do
    files+=("${file}")
done < <(
    find "${repo_root}/include" "${repo_root}/src" "${repo_root}/examples" \
        "${repo_root}/tests" \
        \( -path "${repo_root}/include/third_party" \
        -o -path "${repo_root}/examples/*/.venv" \
        -o -path "${repo_root}/examples/*/output" \
        -o -path "${repo_root}/examples/*/extern" \) -prune \
        -o -type f \
        \( -name '*.c' \
        -o -name '*.cc' \
        -o -name '*.cpp' \
        -o -name '*.cxx' \
        -o -name '*.h' \
        -o -name '*.hh' \
        -o -name '*.hpp' \
        -o -name '*.hxx' \) \
        -print0
)

if [[ "${#files[@]}" -eq 0 ]]; then
    echo "No C/C++ files found"
    exit 0
fi

if [[ "${mode}" == "check" ]]; then
    "${clang_format}" --dry-run -Werror "${files[@]}"
else
    "${clang_format}" -i "${files[@]}"
fi
