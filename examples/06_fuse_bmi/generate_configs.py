#!/usr/bin/env python3
"""
Generate task directories and configuration files for the FUSE BMI example.

Each task is described by a four line file:

    <path to the FUSE file manager for this task>
    <domain name>
    <path to the parameter set, NetCDF>
    <output mode: "output" or "nooutput">

The files are generated rather than stored because they contain absolute
paths that are specific to the machine and to the run.

Typical use:

    python3 generate_configs.py \
        --paramset-dir /path/to/paramsets \
        --task-root /scratch/$USER/io_tasks \
        --outdir /scratch/$USER/configs \
        --file-manager-template cmake/fm_grid.txt.in \
        --settings-dir /path/to/fuse_grid/settings \
        --input-dir /path/to/fuse_grid/input
"""
import argparse
import os
import re


def parse_args():
    here = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(
        description="Generate FUSE BMI task configuration files.")
    p.add_argument("--count", type=int,
                   help="maximum number of discovered parameter sets to use")
    p.add_argument("--outdir", default=os.path.join(here, "data", "configs"),
                   help="directory to write the configuration files into")
    p.add_argument("--task-root", default=os.path.join(here, "data", "tasks"),
                   help="root directory holding one working directory per task")
    p.add_argument("--file-manager-template", required=True,
                   help="portable FUSE file-manager template")
    p.add_argument("--settings-dir", required=True,
                   help="directory containing the FUSE settings files")
    p.add_argument("--input-dir", required=True,
                   help="directory containing the FUSE forcing files")
    p.add_argument("--domain", default="cesm1-cam5",
                   help="FUSE domain name")
    p.add_argument("--paramset-dir", required=True,
                   help="directory holding the parameter set files")
    p.add_argument("--output-mode", default="output",
                   choices=["output", "nooutput"],
                   help="whether the model writes time series output")
    p.add_argument("--width", type=int, default=5,
                   help="zero padded width of the task index")
    return p.parse_args()


def main():
    a = parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    task_root = os.path.abspath(a.task_root)
    paramset_dir = os.path.abspath(a.paramset_dir)
    settings_dir = os.path.abspath(a.settings_dir)
    input_dir = os.path.abspath(a.input_dir)

    pattern = re.compile(r"^paramset_(\d+)\.nc$")
    parameter_sets = []
    for filename in os.listdir(paramset_dir):
        match = pattern.match(filename)
        if match:
            parameter_sets.append((int(match.group(1)), filename))
    parameter_sets.sort()
    if a.count is not None:
        if a.count <= 0:
            raise ValueError("--count must be positive")
        parameter_sets = parameter_sets[:a.count]
    if not parameter_sets:
        raise ValueError("no paramset_*.nc files found in " + paramset_dir)

    with open(a.file_manager_template, encoding="utf-8") as template_file:
        file_manager_template = template_file.read()

    for n, parameter_filename in parameter_sets:
        tag = str(n).zfill(a.width)
        task_dir = os.path.join(task_root, "task_" + tag)
        output_dir = os.path.join(task_dir, "output")
        os.makedirs(output_dir, exist_ok=True)

        file_manager = file_manager_template
        file_manager = file_manager.replace("@FUSE_SETTINGS_DIR@", settings_dir)
        file_manager = file_manager.replace("@FUSE_INPUT_DIR@", input_dir)
        file_manager = file_manager.replace("@FUSE_OUTPUT_DIR@", output_dir)
        file_manager_path = os.path.join(task_dir, "fm_grid.txt")
        with open(file_manager_path, "w", encoding="utf-8") as manager_file:
            manager_file.write(file_manager)

        lines = [
            file_manager_path,
            a.domain,
            os.path.join(paramset_dir, parameter_filename),
            a.output_mode,
        ]
        path = os.path.join(a.outdir, "fuse_bmi_config_" + tag + ".txt")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")

    print("wrote {0} configuration files to {1}".format(
        len(parameter_sets), a.outdir))


if __name__ == "__main__":
    main()
