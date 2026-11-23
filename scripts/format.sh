#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

clang_format="${CLANG_FORMAT:-clang-format-15}"
format_args=(-i)

if [[ "${1:-}" == "--check" ]]; then
    format_args=(--dry-run -Werror)
elif [[ "$#" -gt 0 ]]; then
    echo "usage: scripts/format.sh [--check]" >&2
    exit 2
fi

if ! command -v "${clang_format}" >/dev/null 2>&1; then
    echo "clang-format not found: ${clang_format}" >&2
    exit 1
fi

# Include new source files while excluding ignored build products and vendored code
cd "${repo_root}"
git ls-files -z --cached --others --exclude-standard -- include src examples tests \
    ':!include/third_party' ':!examples/*/extern/**' ':!examples/*/.venv/**' \
    ':!examples/*/output/**' |
    while IFS= read -r -d '' file; do
        case "${file}" in
            *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx)
                if [[ -f "${file}" ]]; then
                    printf '%s\0' "${file}"
                fi
                ;;
        esac
    done | xargs -0 "${clang_format}" "${format_args[@]}"
