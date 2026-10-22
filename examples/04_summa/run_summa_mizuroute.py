"""Run SUMMA through MIAOW and route runoff with mizuRoute."""

import argparse
from datetime import date, time, timedelta
import json
from pathlib import Path
import subprocess

import numpy as np
from netCDF4 import Dataset, date2num, num2date

# Bow River at Banff model paths and run settings
EXAMPLE = Path(__file__).resolve().parent
DATA_DIRECTORY = EXAMPLE / "data/bow"
OUTPUT_DIRECTORY = EXAMPLE / "output/bow"
DRIVER = EXAMPLE.parents[1] / "build-summa/examples/04_summa/miaow_summa"
MIZUROUTE = DRIVER.parent / "route_runoff"
MAX_WORKERS = 4
GRU_COUNT = 52
ROUTING_START = "2002-10-01"
ROUTING_END = "2005-09-30"


def run_model(workers):
    # SUMMA resolves fixture paths from the inherited working directory
    (OUTPUT_DIRECTORY / "miaow/summa").mkdir(parents=True, exist_ok=True)
    subprocess.run([DRIVER, "--config", "config.json",
                    "--max_workers", str(workers),
                    "--output_root", OUTPUT_DIRECTORY / "miaow/jobs"],
                   cwd=DATA_DIRECTORY, check=True)


def run_routing():
    (OUTPUT_DIRECTORY / "miaow/routing").mkdir(parents=True, exist_ok=True)
    subprocess.run([MIZUROUTE, "model/settings/mizuRoute/mizuRoute.control"],
                   cwd=DATA_DIRECTORY, check=True)


def read_runoff(path):
    with Dataset(path) as data:
        time = data["time"]
        dates = num2date(time[:], time.units, only_use_cftime_datetimes=False)

        values = data["averageRoutedRunoff_mean"][:]
        if np.ma.getmaskarray(values).any() or not np.isfinite(values).all():
            raise ValueError(f"Missing or non-finite runoff in {path}")

        return np.asarray(data["gruId"][:]), dates, np.asarray(values), time.units


def read_model_runoff():
    attributes_file = DATA_DIRECTORY / "model/settings/SUMMA/attributes.nc"
    with Dataset(attributes_file) as attributes:
        expected_ids = np.asarray(attributes["gruId"][:])
    assert len(expected_ids) == GRU_COUNT

    runs = sorted((OUTPUT_DIRECTORY / "miaow/jobs").glob("run_*"),
                  key=lambda path: int(path.name.removeprefix("run_")))
    record = json.loads((runs[-1] / "job_0/job_record.json").read_text())
    tasks = record["tasks"]
    if sorted(int(task["metadata"]["feature_id"]) for task in tasks) != sorted(expected_ids):
        raise ValueError("Latest MIAOW job does not contain exactly the expected GRUs")

    by_id = {}
    for path in (OUTPUT_DIRECTORY / "miaow/summa").glob("*_day.nc"):
        gru_ids, gru_dates, values, gru_units = read_runoff(path)
        if not by_id:
            dates, units = gru_dates, gru_units
        else:
            np.testing.assert_array_equal(gru_dates, dates)

        identifier, = map(int, gru_ids)
        if identifier in by_id:
            raise ValueError(f"Duplicate GRU output: {path}")
        by_id[identifier] = values[:, 0]

    if set(by_id) != set(expected_ids):
        raise ValueError("MIAOW output does not contain exactly the expected GRUs")
    values = np.column_stack([by_id[identifier] for identifier in expected_ids])
    return expected_ids, dates, values, units


def prepare_runoff():
    ids, dates, values, units = read_model_runoff()

    # SUMMA labels a completed daily accumulation at midnight on the next day.
    # mizuRoute expects interval starts. Preserve values and exclude the final
    # partial accumulation, whose label is not midnight.
    routing_dates = np.array([value - timedelta(days=1) for value in dates])
    routing_start = date.fromisoformat(ROUTING_START)
    routing_end = date.fromisoformat(ROUTING_END)
    selected = np.array([value.time() == time(0)
                         and routing_start <= value.date() <= routing_end
                         for value in routing_dates])
    retained_dates = routing_dates[selected]
    assert len(retained_dates) == (routing_end - routing_start).days + 1
    assert all((b - a).total_seconds() == 86400
               for a, b in zip(retained_dates, retained_dates[1:]))

    path = OUTPUT_DIRECTORY / "miaow/runoff.nc"
    with Dataset(path, "w", format="NETCDF4_CLASSIC") as output:
        output.createDimension("time", len(retained_dates))
        output.createDimension("gru", len(ids))
        output.createVariable("gruId", "i4", ("gru",))[:] = ids

        time_variable = output.createVariable("time", "f8", ("time",))
        time_variable.units = units
        time_variable.calendar = "standard"
        time_variable.long_name = "start of daily routing interval"
        time_variable[:] = date2num(retained_dates.tolist(), units)

        runoff = output.createVariable("averageRoutedRunoff_mean", "f8", ("time", "gru"))
        runoff.units = "m s-1"
        runoff.cell_methods = "time: mean"
        runoff[:] = values[selected]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--max-workers", type=int, default=MAX_WORKERS)
    args = parser.parse_args()
    run_model(args.max_workers)
    prepare_runoff()
    run_routing()
