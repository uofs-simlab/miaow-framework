#!/usr/bin/env python3
"""Combine the packaged SUMMA CSV forcing into a shared NetCDF fixture."""

import argparse
import csv
from datetime import datetime
import json
import math
from pathlib import Path
import subprocess
import tempfile


def generate_forcing(output):
    data_directory = Path(__file__).resolve().parent / "data" / "gauge_01073000"
    features = json.loads((data_directory / "catchments.geojson").read_text())["features"]
    labels = sorted(feature["properties"]["id"] for feature in features)
    identifiers = []
    source_values = []
    headers = None
    timestamps = None

    # Feature order is explicit so each hyperslab retains its catchment association
    for label in labels:
        if not label.startswith("cat-") or not label[4:].isdigit():
            raise ValueError(f"invalid catchment label: {label}")
        identifier = int(label[4:])
        if identifier > 2**31 - 1 or identifier in identifiers:
            raise ValueError(f"invalid or repeated NetCDF feature ID: {identifier}")
        identifiers.append(identifier)

        source_path = data_directory / "forcings" / f"{label}_forcing.csv"
        with source_path.open(newline="") as source:
            reader = csv.reader(source)
            source_headers = next(reader)
            rows = list(reader)
        if headers is None:
            headers = source_headers
        if (source_headers != headers or len(headers) != 9 or headers[0] != "time"
                or len(set(headers)) != 9):
            raise ValueError(f"unexpected forcing headers in {source_path}")
        if len(rows) != 744 or any(len(row) != 9 for row in rows):
            raise ValueError(f"expected 744 complete hourly records in {source_path}")

        source_times = [datetime.strptime(row[0], "%Y-%m-%d %H:%M:%S") for row in rows]
        if timestamps is None:
            timestamps = source_times
        if source_times != timestamps:
            raise ValueError(f"forcing timestamps differ in {source_path}")
        values = [[float(cell) for cell in row[1:]] for row in rows]
        if any(not math.isfinite(value) for row in values for value in row):
            raise ValueError(f"non-finite forcing value in {source_path}")
        source_values.append(values)

    if len(identifiers) != 5:
        raise ValueError("expected five catchments in the packaged SUMMA example")
    elapsed_seconds = [(time - timestamps[0]).total_seconds() for time in timestamps]
    if elapsed_seconds != [3600.0 * index for index in range(744)]:
        raise ValueError("expected strictly increasing hourly forcing timestamps")

    # Decimal representations round-trip the source binary64 values without conversion
    time_units = f"seconds since {timestamps[0]:%Y-%m-%d %H:%M:%S}"
    lines = [
        "netcdf forcing {",
        "dimensions:",
        "    time = 744;",
        "    feature = 5;",
        "variables:",
        "    int hruId(feature);",
        "    double time(time);",
        '        time:axis = "T";',
        f"        time:units = {json.dumps(time_units)};",
    ]
    lines.extend(f"    double {name}(time, feature);" for name in headers[1:])
    lines.extend([
        "data:",
        "    hruId = " + ", ".join(str(value) for value in identifiers) + ";",
        "    time = " + ", ".join(repr(value) for value in elapsed_seconds) + ";",
    ])
    for variable_index, name in enumerate(headers[1:]):
        lines.append(f"    {name} =")
        for time_index in range(744):
            values = [feature[time_index][variable_index] for feature in source_values]
            terminator = ";" if time_index == 743 else ","
            row = ", ".join(repr(value) for value in values)
            lines.append("        " + row + terminator)
    lines.append("}")

    # ncgen is an offline prerequisite; execution of the example only reads its result
    output = output.resolve()
    with tempfile.TemporaryDirectory(prefix="miaow-summa-forcing-") as temporary:
        cdl_path = Path(temporary) / "forcing.cdl"
        cdl_path.write_text("\n".join(lines) + "\n")
        subprocess.run(["ncgen", "-7", "-o", str(output), str(cdl_path)], check=True)
    print(f"Wrote {output}: 5 features, 744 hours, 8 forcing variables")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "data/gauge_01073000/forcings/forcing.nc",
        help="output NetCDF path (default: the packaged forcing.nc)",
    )
    arguments = parser.parse_args()
    generate_forcing(arguments.output)
