#!/usr/bin/env python3
"""Assemble distributed MIAOW SUMMA runoff into daily mizuRoute input.

Usage:
    .venv/bin/python scripts/assemble_routing_input.py \
        --job-dir JOB_DIR --simulation-start "2002-10-01 00:00:00"
"""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timedelta
import json
import logging
import math
from pathlib import Path
import warnings

import numpy as np
from netCDF4 import Dataset


LOGGER = logging.getLogger(__name__)
TIME_FORMAT = "%Y-%m-%d %H:%M:%S"
SECONDS_PER_DAY = 86400
TIME_UNITS = "seconds since 1990-1-1 0:0:0.0 -0:00"
RUNOFF_NAME = "land_surface_water__runoff_volume_flux"


def read_runoff_csv(path: Path) -> tuple[list[float], list[float]]:
    """Read the time and routed-runoff columns from one declared artifact."""
    with path.open(encoding="utf-8", newline="") as result_file:
        reader = csv.DictReader(result_file)
        fieldnames = reader.fieldnames or []
        if fieldnames.count("time") != 1 or fieldnames.count(RUNOFF_NAME) != 1:
            raise ValueError(f"Runoff CSV has incompatible columns: {path}")

        times: list[float] = []
        runoff: list[float] = []
        for row in reader:
            time_value = float(row["time"])
            runoff_value = float(row[RUNOFF_NAME])
            if not math.isfinite(time_value) or not math.isfinite(runoff_value):
                raise ValueError(f"Runoff CSV contains non-finite values: {path}")
            times.append(time_value)
            runoff.append(runoff_value)

    if not times:
        raise ValueError(f"Runoff CSV contains no data rows: {path}")
    return times, runoff


def select_runoff_artifact(job_dir: Path, artifact_paths: list[str]) -> Path:
    """Select the unique declared CSV artifact containing SUMMA runoff."""
    candidates: list[Path] = []
    for artifact in artifact_paths:
        artifact_path = Path(artifact)
        if not artifact_path.is_absolute():
            artifact_path = job_dir / artifact_path
        if artifact_path.suffix.lower() != ".csv" or not artifact_path.is_file():
            continue
        with artifact_path.open(encoding="utf-8", newline="") as result_file:
            fieldnames = next(csv.reader(result_file), [])
        if "time" in fieldnames and RUNOFF_NAME in fieldnames:
            candidates.append(artifact_path)

    if len(candidates) != 1:
        raise ValueError(
            "Expected one declared CSV artifact containing "
            f"{RUNOFF_NAME}, found {len(candidates)}"
        )
    return candidates[0]


def assemble_routing_input(
    job_dir: Path,
    simulation_start: datetime,
    output_path: Path | None = None,
    reference_path: Path | None = None,
) -> Path:
    """Assemble one completed MIAOW job into daily mizuRoute runoff."""
    job_dir = job_dir.resolve()
    output_path = output_path.resolve() if output_path is not None else None
    reference_path = (
        reference_path.resolve()
        if reference_path is not None
        else None
    )
    if simulation_start.time() != datetime.min.time():
        raise ValueError("Simulation start must be at midnight")
    if not job_dir.is_dir():
        raise ValueError(f"MIAOW job directory does not exist: {job_dir}")
    # Read successful tasks in stable submission order
    job_record_path = job_dir / "job_record.json"
    if not job_record_path.is_file():
        raise ValueError(f"MIAOW job record does not exist: {job_record_path}")
    job_record = json.loads(job_record_path.read_text(encoding="utf-8"))
    tasks = job_record.get("tasks", [])
    if not tasks:
        raise ValueError("MIAOW job record contains no tasks")
    tasks.sort(key=lambda task: int(task["task_id"]))

    feature_ids: list[int] = []
    result_paths: list[Path] = []
    for task in tasks:
        result = task["result"]
        if result["status"] != "succeeded":
            raise ValueError(f"MIAOW task {task['task_id']} did not succeed")
        feature_id = int(task["metadata"]["feature_id"])
        feature_ids.append(feature_id)
        result_paths.append(
            select_runoff_artifact(job_dir, result.get("artifact_paths", []))
        )
    if len(set(feature_ids)) != len(feature_ids):
        raise ValueError("MIAOW job record contains duplicate feature IDs")

    # Derive the common timestep and complete daily group boundaries
    common_times: list[float] | None = None
    group_ranges: list[tuple[int, int]] = []
    routing_relative_times: list[float] = []
    trailing_value_count = 0
    daily_runoff_by_gru: list[list[float]] = []
    time_step = 0.0
    for feature_id, result_path in zip(feature_ids, result_paths, strict=True):
        times, runoff = read_runoff_csv(result_path)
        if common_times is None:
            if len(times) < 2:
                raise ValueError("SUMMA runoff requires at least two timesteps")
            time_step = times[1] - times[0]
            if time_step <= 0.0 or not math.isclose(
                times[0], time_step, rel_tol=0.0, abs_tol=1e-9
            ):
                raise ValueError("SUMMA result time must begin after one positive step")
            if any(
                not math.isclose(
                    current - previous,
                    time_step,
                    rel_tol=0.0,
                    abs_tol=1e-9,
                )
                for previous, current in zip(times, times[1:])
            ):
                raise ValueError("SUMMA result time must have constant spacing")

            samples_per_day_value = SECONDS_PER_DAY / time_step
            samples_per_day = round(samples_per_day_value)
            if not math.isclose(
                samples_per_day_value,
                samples_per_day,
                rel_tol=0.0,
                abs_tol=1e-9,
            ):
                raise ValueError("SUMMA timestep must divide 86400 seconds exactly")

            first_group_end = samples_per_day + 1
            if len(times) < first_group_end:
                raise ValueError("SUMMA result contains no complete daily group")
            group_ranges.append((0, first_group_end))
            group_start = first_group_end
            while len(times) - group_start >= samples_per_day:
                group_end = group_start + samples_per_day
                group_ranges.append((group_start, group_end))
                group_start = group_end
            trailing_value_count = len(times) - group_start
            routing_relative_times = [
                float(day_index * SECONDS_PER_DAY)
                for day_index in range(len(group_ranges))
            ]
            common_times = times
        elif times != common_times:
            raise ValueError(f"GRU {feature_id} has a different result time coordinate")

        # Preserve SUMMA's time-ordered statistic accumulation
        daily_runoff_by_gru.append(
            [
                sum(runoff[group_start:group_end]) / (group_end - group_start)
                for group_start, group_end in group_ranges
            ]
        )

    epoch = datetime(1990, 1, 1)
    start_seconds = (simulation_start - epoch).total_seconds()
    encoded_times = [
        start_seconds + relative_time for relative_time in routing_relative_times
    ]

    if output_path is None:
        last_date = (
            simulation_start + timedelta(seconds=routing_relative_times[-1])
        ).date()
        output_path = job_dir / "results" / (
            f"runoff_{simulation_start.date().isoformat()}_to_{last_date.isoformat()}.nc"
        )
    if output_path.exists():
        raise ValueError(f"Output file already exists: {output_path}")

    # Write the minimal one-dimensional mizuRoute runoff contract
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with Dataset(output_path, "w", format="NETCDF4") as output:
        output.createDimension("time", None)
        output.createDimension("gru", len(feature_ids))

        time_variable = output.createVariable(
            "time", "f8", ("time",), fill_value=False
        )
        time_variable.long_name = "time since time reference (instant)"
        time_variable.units = TIME_UNITS
        time_variable[:] = encoded_times

        gru_variable = output.createVariable(
            "gru", "i4", ("gru",), fill_value=False
        )
        gru_variable.long_name = "gruId in the input file"
        gru_variable.units = "-"
        gru_variable[:] = range(1, len(feature_ids) + 1)

        gru_id_variable = output.createVariable(
            "gruId", "i8", ("gru",), fill_value=False
        )
        gru_id_variable.long_name = (
            "ID defining the grouped (basin) response unit"
        )
        gru_id_variable.units = "-"
        gru_id_variable[:] = feature_ids

        runoff_variable = output.createVariable(
            "averageRoutedRunoff_mean",
            "f8",
            ("time", "gru"),
            fill_value=False,
        )
        runoff_variable.long_name = "routed runoff (mean)"
        runoff_variable.units = "m s-1"
        # netCDF4 1.7.4 warns on multidimensional unlimited writes with NumPy 2.5
        with warnings.catch_warnings():
            warnings.filterwarnings(
                "ignore",
                message="Setting the shape on a NumPy array has been deprecated",
                category=DeprecationWarning,
            )
            runoff_variable[:, :] = [
                list(runoff_values)
                for runoff_values in zip(*daily_runoff_by_gru, strict=True)
            ]

    LOGGER.info("MIAOW job: %s", job_record_path)
    LOGGER.info("GRUs: %d", len(feature_ids))
    LOGGER.info("SUMMA timestep: %.17g seconds", time_step)
    LOGGER.info("Daily records: %d", len(encoded_times))
    LOGGER.info("Discarded trailing values per GRU: %d", trailing_value_count)
    LOGGER.info(
        "Daily runoff range: %.17g to %.17g m s-1",
        min(min(values) for values in daily_runoff_by_gru),
        max(max(values) for values in daily_runoff_by_gru),
    )
    LOGGER.info(
        "Routing time range: %s to %s",
        simulation_start.strftime(TIME_FORMAT),
        (
            simulation_start + timedelta(seconds=routing_relative_times[-1])
        ).strftime(TIME_FORMAT),
    )
    LOGGER.info("Routing input: %s", output_path)

    if reference_path is None:
        return output_path

    # Compare the coordinates and runoff consumed by mizuRoute
    with Dataset(output_path) as generated, Dataset(reference_path) as reference:
        for name in ("time", "gru", "gruId"):
            if generated.variables[name][:].tolist() != reference.variables[name][
                :
            ].tolist():
                raise ValueError(f"Reference {name} values do not match")

        generated_runoff = generated.variables["averageRoutedRunoff_mean"][:]
        reference_runoff = reference.variables["averageRoutedRunoff_mean"][:]
        if generated_runoff.shape != reference_runoff.shape:
            raise ValueError("Reference runoff shape does not match")

        absolute_errors = np.abs(generated_runoff - reference_runoff)
        relative_errors = np.divide(
            absolute_errors,
            np.abs(reference_runoff),
            out=np.zeros_like(absolute_errors),
            where=reference_runoff != 0.0,
        )
        mismatch_count = int(
            np.count_nonzero(
                ~np.isclose(
                    generated_runoff,
                    reference_runoff,
                    rtol=1e-6,
                    atol=1e-10,
                )
            )
        )
        worst_index = np.unravel_index(
            np.argmax(absolute_errors), absolute_errors.shape
        )
        maximum_absolute_error = float(absolute_errors[worst_index])
        maximum_relative_error = float(np.max(relative_errors))

    worst_time_index, worst_gru_index = worst_index
    LOGGER.info("Reference routing input: %s", reference_path)
    LOGGER.info("Compared runoff values: %d", generated_runoff.size)
    LOGGER.info("Runoff mismatches: %d", mismatch_count)
    LOGGER.info("Maximum absolute error: %.17g m s-1", maximum_absolute_error)
    LOGGER.info("Maximum relative error: %.17g", maximum_relative_error)
    LOGGER.info(
        "Worst difference: GRU %d at %s",
        feature_ids[worst_gru_index],
        (
            simulation_start
            + timedelta(seconds=routing_relative_times[worst_time_index])
        ).strftime(TIME_FORMAT),
    )
    if mismatch_count != 0:
        raise ValueError("Generated routing runoff does not match the reference")

    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Assemble MIAOW SUMMA runoff into daily mizuRoute NetCDF."
    )
    parser.add_argument(
        "--job-dir",
        type=Path,
        required=True,
        help="Completed MIAOW job directory containing job_record.json.",
    )
    parser.add_argument(
        "--simulation-start",
        required=True,
        help="Absolute SUMMA time zero as YYYY-MM-DD HH:MM:SS.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Output path; defaults to a dated file under JOB_DIR/results.",
    )
    parser.add_argument(
        "--reference-routing-input",
        type=Path,
        help="Optional version-matched routing input for validation.",
    )
    arguments = parser.parse_args()
    assemble_routing_input(
        arguments.job_dir,
        datetime.strptime(arguments.simulation_start, TIME_FORMAT),
        arguments.output,
        arguments.reference_routing_input,
    )


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    main()
