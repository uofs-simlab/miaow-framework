#!/usr/bin/env python3
"""
Generate task configuration files for the FUSE BMI example.

Each task is described by a four line file:

    <path to the FUSE file manager for this task>
    <domain name>
    <path to the parameter set, NetCDF>
    <output mode: "output" or "nooutput">

The files are generated rather than stored because they contain absolute
paths that are specific to the machine and to the run, and because a
sensitivity study may need any number of them.

Typical use:

    python3 generate_configs.py --count 512 \
        --task-root /scratch/$USER/io_tasks \
        --paramset-dir /path/to/paramsets
"""
import argparse
import os


def parse_args():
    here = os.path.dirname(os.path.abspath(__file__))
    p = argparse.ArgumentParser(
        description="Generate FUSE BMI task configuration files.")
    p.add_argument("--count", type=int, default=512,
                   help="number of configuration files to write")
    p.add_argument("--start", type=int, default=0,
                   help="index of the first task")
    p.add_argument("--outdir", default=os.path.join(here, "data", "configs"),
                   help="directory to write the configuration files into")
    p.add_argument("--task-root", default=os.path.join(here, "data", "tasks"),
                   help="root directory holding one working directory per task")
    p.add_argument("--file-manager", default="fm_grid.txt",
                   help="name of the FUSE file manager inside each task directory")
    p.add_argument("--domain", default="cesm1-cam5",
                   help="FUSE domain name")
    p.add_argument("--paramset-dir", required=True,
                   help="directory holding the parameter set files")
    p.add_argument("--paramset-prefix", default="paramset_",
                   help="filename prefix of the parameter set files")
    p.add_argument("--paramset-suffix", default=".nc",
                   help="filename suffix of the parameter set files")
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

    for n in range(a.start, a.start + a.count):
        tag = str(n).zfill(a.width)
        lines = [
            os.path.join(task_root, "task_" + tag, a.file_manager),
            a.domain,
            os.path.join(paramset_dir,
                         a.paramset_prefix + tag + a.paramset_suffix),
            a.output_mode,
        ]
        path = os.path.join(a.outdir, "fuse_bmi_config_" + tag + ".txt")
        with open(path, "w") as fh:
            fh.write("\n".join(lines) + "\n")

    print("wrote {0} configuration files to {1}".format(a.count, a.outdir))


if __name__ == "__main__":
    main()
