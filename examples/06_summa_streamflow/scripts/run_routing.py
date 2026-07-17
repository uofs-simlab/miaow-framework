#!/usr/bin/env python3
"""Route an assembled SUMMA runoff file with mizuRoute.

Usage:
    .venv/bin/python scripts/run_routing.py \
        --runoff-input JOB_DIR/results/runoff_START_to_END.nc \
        --source-settings MIZUROUTE_SETTINGS --mizuroute-executable mizuRoute.exe
"""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import logging
from pathlib import Path
import re
import shutil
import subprocess

from netCDF4 import Dataset, num2date

LOGGER = logging.getLogger(__name__)
REQUIRED_SETTINGS = (
    "mizuRoute.control",
    "topology.nc",
    "param.nml.default",
)
CONTROL_ENTRY = re.compile(r"^(\s*)(<[^>]+>)(\s+)(.*?)(\s+!.*)$")


def control_values(lines: list[str]) -> dict[str, str]:
    """Read uncommented values from mizuRoute control lines."""
    return {
        match.group(2): match.group(4).strip()
        for line in lines
        if (match := CONTROL_ENTRY.match(line)) is not None
    }


def run_routing(
    runoff_path: Path,
    source_settings_dir: Path,
    executable_path: Path,
) -> Path:
    """Route one assembled runoff file into job-owned streamflow output."""
    runoff_path = runoff_path.resolve()
    job_dir = runoff_path.parent.parent
    source_settings_dir = source_settings_dir.resolve()
    executable_path = executable_path.resolve()
    if not executable_path.is_file():
        raise ValueError(f"mizuRoute executable does not exist: {executable_path}")

    # Read the routing period and basin association from assembled runoff
    with Dataset(runoff_path) as runoff:
        time_variable = runoff.variables["time"]
        time_values = time_variable[:]
        converted_dates = num2date(
            [time_values[0], time_values[-1]],
            units=time_variable.units,
            calendar=getattr(time_variable, "calendar", "standard"),
            only_use_cftime_datetimes=False,
        )
        first_date = datetime(
            converted_dates[0].year,
            converted_dates[0].month,
            converted_dates[0].day,
        )
        last_date = datetime(
            converted_dates[1].year,
            converted_dates[1].month,
            converted_dates[1].day,
        )
        input_times = time_values.tolist()
        input_gru_ids = runoff.variables["gruId"][:].tolist()

    date_range = f"{first_date.date().isoformat()}_to_{last_date.date().isoformat()}"
    work_dir = job_dir / f"mizuroute_{date_range}"
    output_path = job_dir / "results" / f"streamflow_{date_range}.nc"
    if work_dir.exists() and any(work_dir.iterdir()):
        raise ValueError(f"mizuRoute directory is not empty: {work_dir}")
    if output_path.exists():
        raise ValueError(f"Streamflow output already exists: {output_path}")

    # Stage the immutable settings and minimally rewritten control
    settings_dir = work_dir / "settings"
    raw_output_dir = work_dir / "raw"
    settings_dir.mkdir(parents=True)
    raw_output_dir.mkdir()
    for filename in REQUIRED_SETTINGS[1:]:
        shutil.copy2(source_settings_dir / filename, settings_dir / filename)

    source_control_lines = (
        source_settings_dir / "mizuRoute.control"
    ).read_text(encoding="utf-8").splitlines()
    source_values = control_values(source_control_lines)
    if source_values["<sim_start>"] != first_date.date().isoformat():
        raise ValueError("mizuRoute simulation start does not match runoff input")
    if source_values["<sim_end>"] != last_date.date().isoformat():
        raise ValueError("mizuRoute simulation end does not match runoff input")
    case_name = source_values["<case_name>"]

    rewritten_values = {
        "<ancil_dir>": settings_dir.as_posix().rstrip("/") + "/",
        "<input_dir>": runoff_path.parent.as_posix().rstrip("/") + "/",
        "<output_dir>": raw_output_dir.as_posix().rstrip("/") + "/",
        "<fname_qsim>": runoff_path.name,
    }
    rewritten_keys: set[str] = set()
    staged_control_lines: list[str] = []
    for line in source_control_lines:
        match = CONTROL_ENTRY.match(line)
        if match is None or match.group(2) not in rewritten_values:
            staged_control_lines.append(line)
            continue
        key = match.group(2)
        staged_control_lines.append(
            f"{match.group(1)}{key}{match.group(3)}"
            f"{rewritten_values[key]}{match.group(5)}"
        )
        rewritten_keys.add(key)
    if rewritten_keys != set(rewritten_values):
        raise ValueError("mizuRoute control is missing a rewritten setting")

    staged_control_path = settings_dir / "mizuRoute.control"
    staged_control_path.write_text(
        "\n".join(staged_control_lines) + "\n", encoding="utf-8"
    )

    # Preserve mizuRoute and merge output in one job-local log
    log_path = work_dir / "mizuRoute.log"
    command = [str(executable_path), str(staged_control_path)]
    LOGGER.info("Running mizuRoute; complete output: %s", log_path)
    with log_path.open("w", encoding="utf-8") as log_file:
        log_file.write(f"command: {' '.join(command)}\n")
        log_file.flush()
        subprocess.run(
            command,
            cwd=work_dir,
            check=True,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
        )

    raw_output_paths = sorted(raw_output_dir.glob(f"{case_name}*.nc"))
    if not raw_output_paths:
        raise ValueError("mizuRoute produced no NetCDF output files")
    ncrcat_path = shutil.which("ncrcat")
    if ncrcat_path is None:
        raise ValueError("ncrcat is required to merge mizuRoute output")
    merge_command = [
        ncrcat_path,
        "-O",
        "-h",
        *(str(path) for path in raw_output_paths),
        str(output_path),
    ]
    with log_path.open("a", encoding="utf-8") as log_file:
        log_file.write(f"merge_command: {' '.join(merge_command)}\n")
        log_file.flush()
        subprocess.run(
            merge_command,
            check=True,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
        )

    # Confirm that routing preserved the runoff time and basin association
    with Dataset(output_path) as streamflow:
        if streamflow.variables["time"][:].tolist() != input_times:
            raise ValueError("mizuRoute times do not match the runoff input")
        if streamflow.variables["reachID"][:].tolist() != input_gru_ids:
            raise ValueError("mizuRoute reach IDs do not match runoff GRU IDs")
        routed_runoff = streamflow.variables["IRFroutedRunoff"]
        if routed_runoff.dimensions != ("time", "seg"):
            raise ValueError("IRFroutedRunoff has unexpected dimensions")
        route_version = streamflow.getncattr("mizuRoute-version")
        route_hash = streamflow.getncattr("gitHash")

    executable_hash = hashlib.sha256(executable_path.read_bytes()).hexdigest()
    with log_path.open("a", encoding="utf-8") as log_file:
        log_file.write(f"raw_output_files: {len(raw_output_paths)}\n")
        log_file.write(f"streamflow_output: {output_path}\n")
        log_file.write(f"mizuRoute_version: {route_version}\n")
        log_file.write(f"mizuRoute_git_hash: {route_hash}\n")
        log_file.write(f"mizuRoute_executable_sha256: {executable_hash}\n")

    LOGGER.info("Runoff input: %s", runoff_path)
    LOGGER.info("Streamflow output: %s", output_path)
    LOGGER.info("mizuRoute version: %s (%s)", route_version, route_hash)
    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Route an assembled SUMMA runoff file with mizuRoute."
    )
    parser.add_argument("--runoff-input", type=Path, required=True)
    parser.add_argument("--source-settings", type=Path, required=True)
    parser.add_argument("--mizuroute-executable", type=Path, required=True)
    arguments = parser.parse_args()
    run_routing(
        arguments.runoff_input,
        arguments.source_settings,
        arguments.mizuroute_executable,
    )


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    main()
