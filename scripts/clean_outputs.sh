#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

generated_paths=(
    "output"
    "examples/00_hello/output"
    "examples/01_heat/output"
    "examples/03_dynamic_bmi/output"
    "examples/04_summa/output"
    "examples/05_libtrixi_bmi/output"
    "examples/06_fuse_bmi/output"
    "examples/07_job_controller/output"
    "examples/08_particle_swarm/output"
)

removed_any=false
for relative_path in "${generated_paths[@]}"; do
    absolute_path="${repo_root}/${relative_path}"
    if [[ -e "${absolute_path}" ]]; then
        rm -rf "${absolute_path}"
        echo "Removed ${relative_path}"
        removed_any=true
    fi
done

if [[ "${removed_any}" = false ]]; then
    echo "No known generated output directories found"
fi
