#!/usr/bin/env python3
"""Write per-catchment SUMMA forcing CSV fixtures from the gauge NetCDF file."""

from __future__ import annotations

import argparse
import csv
import logging
from datetime import datetime, timedelta
from pathlib import Path

LOGGER = logging.getLogger("stage_summa_gauge_forcing_csvs")

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = (
    PROJECT_ROOT
    / "third_party"
    / "summa"
    / "test_ngen"
    / "gauge_01073000"
    / "forcing"
    / "ngen_forcing_from_summa.nc"
)
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "examples" / "04_summa" / "data" / "forcings"

EXPECTED_IDS = {
    "cat-11223",
    "cat-11224",
    "cat-11371",
    "cat-11410",
    "cat-11509",
}
FORCING_VARIABLES = [
    "precip_rate",
    "TMP_2maboveground",
    "SPFH_2maboveground",
    "UGRD_10maboveground",
    "VGRD_10maboveground",
    "DSWRF_surface",
    "DLWRF_surface",
    "PRES_surface",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stage SUMMA gauge forcing CSVs for the MIAOW example."
    )
    parser.add_argument(
        "--source",
        type=Path,
        default=DEFAULT_SOURCE,
        help="Path to test_ngen/gauge_01073000/forcing/ngen_forcing_from_summa.nc",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Directory that receives cat-*_forcing.csv files",
    )
    return parser.parse_args()


def read_start_time(units: str) -> datetime:
    prefix = "hours since "
    if not units.startswith(prefix):
        raise RuntimeError(f"Unsupported time units: {units}")

    timestamp = units.removeprefix(prefix).strip()
    if len(timestamp) == 10:
        timestamp = f"{timestamp} 00:00:00"
    return datetime.strptime(timestamp, "%Y-%m-%d %H:%M:%S")


def main() -> int:
    args = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    if not args.source.is_file():
        raise FileNotFoundError(f"SUMMA forcing NetCDF not found: {args.source}")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    try:
        from netCDF4 import Dataset
    except ImportError as exc:
        raise RuntimeError("Install netCDF4 to regenerate SUMMA forcing CSVs") from exc

    with Dataset(args.source) as dataset:
        required_variables = ["ids", "time", *FORCING_VARIABLES]
        missing_variables = [
            name for name in required_variables if name not in dataset.variables
        ]
        if missing_variables:
            raise RuntimeError(
                "Missing NetCDF variable(s): " + ", ".join(missing_variables)
            )

        ids = [str(value) for value in dataset.variables["ids"][:]]
        missing_ids = sorted(EXPECTED_IDS.difference(ids))
        if missing_ids:
            raise RuntimeError("Missing catchment id(s): " + ", ".join(missing_ids))

        time_variable = dataset.variables["time"]
        start_time = read_start_time(str(time_variable.units))
        time_values = list(time_variable[:])

        for catchment_id in sorted(EXPECTED_IDS):
            catchment_index = ids.index(catchment_id)
            output_path = args.output_dir / f"{catchment_id}_forcing.csv"

            with output_path.open("w", newline="") as output_file:
                writer = csv.writer(output_file, lineterminator="\n")
                writer.writerow(["time", *FORCING_VARIABLES])
                for time_value_index, time_value in enumerate(time_values):
                    timestamp = start_time + timedelta(hours=float(time_value))
                    row = [timestamp.strftime("%Y-%m-%d %H:%M:%S")]
                    for variable_name in FORCING_VARIABLES:
                        variable = dataset.variables[variable_name]
                        row.append(float(variable[catchment_index, time_value_index]))
                    writer.writerow(row)

            LOGGER.info("Wrote %s", output_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
