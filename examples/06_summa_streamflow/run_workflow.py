#!/usr/bin/env python3
"""Run distributed SUMMA through MIAOW, then assemble and route its runoff.

Usage:
    .venv/bin/python run_workflow.py
    .venv/bin/python run_workflow.py --clear-runs
"""

from __future__ import annotations

import argparse
import logging
from pathlib import Path
import shutil
import subprocess
import sys


LOGGER = logging.getLogger(__name__)
EXAMPLE_DIR = Path(__file__).resolve().parent
OUTPUT_ROOT = EXAMPLE_DIR / "output/bow_at_banff"
REFERENCE_ROOT = (
    EXAMPLE_DIR.parents[1] / "_runs/06_summa_streamflow_reference_092013ac"
)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run MIAOW SUMMA and mizuRoute as one example workflow."
    )
    parser.add_argument(
        "--miaow-executable",
        type=Path,
        default=EXAMPLE_DIR / "miaow_summa_streamflow",
    )
    parser.add_argument(
        "--mizuroute-executable",
        type=Path,
        default=Path(shutil.which("mizuRoute.exe") or "mizuRoute.exe"),
    )
    parser.add_argument(
        "--source-settings",
        type=Path,
        default=REFERENCE_ROOT / "model/settings/mizuRoute",
    )
    parser.add_argument(
        "--reference-routing-input",
        type=Path,
        default=REFERENCE_ROOT / "reference/092013ac/SUMMA/run1_day.nc",
    )
    parser.add_argument(
        "--simulation-start",
        default="2002-10-01 00:00:00",
    )
    parser.add_argument("--max-workers", type=int, default=4)
    parser.add_argument(
        "--clear-runs",
        action="store_true",
        help="Remove existing run_* directories before starting.",
    )
    arguments = parser.parse_args()

    # Clear only runtime-owned runs when explicitly requested
    if arguments.clear_runs:
        for run_dir in OUTPUT_ROOT.glob("run_*"):
            shutil.rmtree(run_dir)
        LOGGER.info("Cleared existing runs under %s", OUTPUT_ROOT)

    # Run one MIAOW job, which creates the next numbered run directory
    miaow_command = [
        str(arguments.miaow_executable.resolve()),
        "--max_workers",
        str(arguments.max_workers),
    ]
    subprocess.run(miaow_command, cwd=EXAMPLE_DIR, check=True)

    run_dirs = [
        path
        for path in OUTPUT_ROOT.glob("run_*")
        if path.name.removeprefix("run_").isdigit()
    ]
    latest_run = max(
        run_dirs,
        key=lambda path: int(path.name.removeprefix("run_")),
    )
    job_dir = latest_run / "job_0"

    # Assemble MIAOW results into the current routing-input contract
    assembly_command = [
        sys.executable,
        str(EXAMPLE_DIR / "scripts/assemble_routing_input.py"),
        "--job-dir",
        str(job_dir),
        "--simulation-start",
        arguments.simulation_start,
        "--reference-routing-input",
        str(arguments.reference_routing_input.resolve()),
    ]
    subprocess.run(assembly_command, cwd=EXAMPLE_DIR, check=True)
    runoff_path = next((job_dir / "results").glob("runoff_*.nc"))

    # Route the assembled runoff without reading MIAOW task artifacts
    routing_command = [
        sys.executable,
        str(EXAMPLE_DIR / "scripts/run_routing.py"),
        "--runoff-input",
        str(runoff_path),
        "--source-settings",
        str(arguments.source_settings.resolve()),
        "--mizuroute-executable",
        str(arguments.mizuroute_executable.resolve()),
    ]
    subprocess.run(routing_command, cwd=EXAMPLE_DIR, check=True)
    LOGGER.info("Completed workflow: %s", job_dir)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    main()
