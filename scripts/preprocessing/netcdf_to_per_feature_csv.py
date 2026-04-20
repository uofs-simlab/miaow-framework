#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import logging
import math
from pathlib import Path
from typing import Any

import pandas as pd
import xarray as xr


LOGGER = logging.getLogger("netcdf_to_per_feature_csv")


SCALAR_WIND_SPEED_NAME = "wind_speed"
IDENTITY_TRANSFORM = "identity"
RATE_TO_ACCUMULATION_TRANSFORM = "rate_to_accumulation"

# Keep these input aliases aligned with SYMFLUENCE's forcing standardization:
# symfluence.data.utils.variable_utils.VariableStandardizer.RENAME_MAPS
AORC_INPUT_TO_BMI = {
    "APCP_surface": "atmosphere_water__liquid_equivalent_precipitation_rate",
    "DSWRF_surface": "land_surface_radiation~incoming~shortwave__energy_flux",
    "DLWRF_surface": "land_surface_radiation~incoming~longwave__energy_flux",
    "SPFH_2maboveground": "atmosphere_air_water~vapor__specific_humidity",
    "UGRD_10maboveground": "land_surface_wind__x_component_of_velocity",
    "VGRD_10maboveground": "land_surface_wind__y_component_of_velocity",
    "PRES_surface": "land_surface_air__pressure",
    "TMP_2maboveground": "land_surface_air__temperature",
}

NEXTGEN_INPUT_TO_BMI = {
    "RAINRATE": "atmosphere_water__liquid_equivalent_precipitation_rate",
    "SWDOWN": "land_surface_radiation~incoming~shortwave__energy_flux",
    "LWDOWN": "land_surface_radiation~incoming~longwave__energy_flux",
    "Q2D": "atmosphere_air_water~vapor__specific_humidity",
    "U2D": "land_surface_wind__x_component_of_velocity",
    "V2D": "land_surface_wind__y_component_of_velocity",
    "PSFC": "land_surface_air__pressure",
    "T2D": "land_surface_air__temperature",
}

SUMMA_INPUT_TO_BMI = {
    "airtemp": "land_surface_air__temperature",
    "pptrate": "atmosphere_water__liquid_equivalent_precipitation_rate",
    "SWRadAtm": "land_surface_radiation~incoming~shortwave__energy_flux",
    "LWRadAtm": "land_surface_radiation~incoming~longwave__energy_flux",
    "spechum": "atmosphere_air_water~vapor__specific_humidity",
    "windspd_u": "land_surface_wind__x_component_of_velocity",
    "windspd_v": "land_surface_wind__y_component_of_velocity",
    "airpres": "land_surface_air__pressure",
}

INPUT_TO_BMI = {
    **AORC_INPUT_TO_BMI,
    **NEXTGEN_INPUT_TO_BMI,
    **SUMMA_INPUT_TO_BMI,
}

REQUIRED_BMI_VARIABLES = (
    "atmosphere_water__liquid_equivalent_precipitation_rate",
    "land_surface_air__temperature",
    "atmosphere_air_water~vapor__specific_humidity",
    "land_surface_air__pressure",
    "land_surface_radiation~incoming~shortwave__energy_flux",
    "land_surface_radiation~incoming~longwave__energy_flux",
    "land_surface_wind__x_component_of_velocity",
    "land_surface_wind__y_component_of_velocity",
)
PREFERRED_FEATURE_DIMS = ("hru", "feature", "catchment-id", "catchment", "id")
PREFERRED_ID_NAMES = ("hruId", "ids", "feature_id", "catchment_id", "id")

# Output columns are defined as (CSV header, normalized source variable, transform)
OUTPUT_SCHEMAS = {
    "bmi": {
        "time_format": "%Y-%m-%d %H:%M:%S",
        "columns": tuple(
            (variable_name, variable_name, IDENTITY_TRANSFORM)
            for variable_name in REQUIRED_BMI_VARIABLES
        ),
    },
    "native-aorc": {
        "time_format": "%Y/%m/%d %H:%M:%S",
        # Native parsers expect APCP_surface as a per-timestep accumulation
        "columns": (
            (
                "APCP_surface",
                "atmosphere_water__liquid_equivalent_precipitation_rate",
                RATE_TO_ACCUMULATION_TRANSFORM,
            ),
            (
                "DLWRF_surface",
                "land_surface_radiation~incoming~longwave__energy_flux",
                IDENTITY_TRANSFORM,
            ),
            (
                "DSWRF_surface",
                "land_surface_radiation~incoming~shortwave__energy_flux",
                IDENTITY_TRANSFORM,
            ),
            (
                "PRES_surface",
                "land_surface_air__pressure",
                IDENTITY_TRANSFORM,
            ),
            (
                "SPFH_2maboveground",
                "atmosphere_air_water~vapor__specific_humidity",
                IDENTITY_TRANSFORM,
            ),
            (
                "TMP_2maboveground",
                "land_surface_air__temperature",
                IDENTITY_TRANSFORM,
            ),
            (
                "UGRD_10maboveground",
                "land_surface_wind__x_component_of_velocity",
                IDENTITY_TRANSFORM,
            ),
            (
                "VGRD_10maboveground",
                "land_surface_wind__y_component_of_velocity",
                IDENTITY_TRANSFORM,
            ),
            (
                "precip_rate",
                "atmosphere_water__liquid_equivalent_precipitation_rate",
                IDENTITY_TRANSFORM,
            ),
        ),
    },
}


def configure_logging(verbose: bool) -> None:
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(level=level, format="%(levelname)s %(message)s")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert model-ready NetCDF forcing files into per-feature CSVs for "
            "MIAOW BMI forcing or native AORC-style forcing."
        )
    )
    parser.add_argument(
        "forcing_dir",
        type=Path,
        help="Directory containing model-ready NetCDF forcing files",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Directory where per-feature CSV files will be written",
    )
    parser.add_argument(
        "--feature-dim",
        default=None,
        help="Feature dimension name; inferred automatically when omitted",
    )
    parser.add_argument(
        "--id-variable",
        default=None,
        help="Feature ID variable/coordinate name; inferred automatically when omitted",
    )
    parser.add_argument(
        "--feature-id-prefix",
        default="cat-",
        help="Prefix added to numeric feature IDs",
    )
    parser.add_argument(
        "--filename-template",
        default="{feature_id}.csv",
        help="Output filename template using {feature_id}",
    )
    parser.add_argument(
        "--output-schema",
        choices=tuple(OUTPUT_SCHEMAS),
        default="bmi",
        help="CSV schema to write",
    )
    parser.add_argument(
        "--single-feature-id",
        default="cat-1",
        help="Feature ID to use when the dataset has no feature dimension",
    )
    parser.add_argument(
        "--start",
        default=None,
        help="Optional inclusive simulation start time",
    )
    parser.add_argument(
        "--end",
        default=None,
        help="Optional inclusive simulation end time",
    )
    parser.add_argument(
        "--pad-lookahead-steps",
        type=int,
        default=0,
        help="Repeat the last timestep this many times after the input record ends",
    )
    parser.add_argument(
        "--assume-westerly-wind",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Decompose scalar wind speed to U/V as U=wind_speed, V=0 when needed",
    )
    parser.add_argument(
        "--realization-path",
        type=Path,
        default=None,
        help="Optional path for a config-only MIAOW realization.json stub",
    )
    parser.add_argument(
        "--config-template",
        default="./configs/{feature_id}.txt",
        help="Config path template used when writing realization.json",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable debug logging",
    )
    return parser.parse_args()


def parse_time_or_none(raw_value: str | None) -> pd.Timestamp | None:
    if raw_value is None:
        return None
    return pd.Timestamp(raw_value)


def load_forcing_dataset(forcing_dir: Path) -> xr.Dataset:
    forcing_files = sorted(forcing_dir.glob("*.nc"))
    if not forcing_files:
        raise FileNotFoundError(f"No NetCDF forcing files found in {forcing_dir}")

    LOGGER.info(
        "Loading %d NetCDF forcing file(s) from %s",
        len(forcing_files),
        forcing_dir,
    )

    if len(forcing_files) == 1:
        with xr.open_dataset(forcing_files[0]) as dataset:
            combined = dataset.load()
    else:
        datasets = [xr.open_dataset(path) for path in forcing_files]
        try:
            combined = xr.concat(
                datasets,
                dim="time",
                data_vars="minimal",
                coords="minimal",
                compat="override",
            ).load()
        finally:
            for dataset in datasets:
                dataset.close()

    if "time" not in combined.coords and "time" not in combined.dims:
        raise ValueError("Forcing dataset does not define a 'time' coordinate")

    combined = combined.sortby("time")
    combined = combined.drop_duplicates(dim="time")
    return combined


def normalize_to_bmi_variables(dataset: xr.Dataset) -> xr.Dataset:
    renames = {
        source: target
        for source, target in INPUT_TO_BMI.items()
        if source in dataset.data_vars and target not in dataset.data_vars
    }
    if (
        SCALAR_WIND_SPEED_NAME not in dataset.data_vars
        and "windspd" in dataset.data_vars
    ):
        renames["windspd"] = SCALAR_WIND_SPEED_NAME

    if renames:
        LOGGER.info(
            "Normalizing forcing variable names: %s",
            ", ".join(sorted(renames)),
        )
        dataset = dataset.rename(renames)

    return dataset


def subset_to_time_window(
    dataset: xr.Dataset,
    start_time: pd.Timestamp | None,
    end_time: pd.Timestamp | None,
) -> xr.Dataset:
    if start_time is None and end_time is None:
        return dataset

    if start_time is None:
        start_time = pd.Timestamp(dataset["time"].values[0])
    if end_time is None:
        end_time = pd.Timestamp(dataset["time"].values[-1])

    subset = dataset.sel(time=slice(start_time, end_time))
    if subset.sizes["time"] == 0:
        raise ValueError(
            "Requested time window produced an empty dataset: "
            f"{start_time} to {end_time}"
        )

    LOGGER.info(
        "Subset forcing to %s through %s (%d timestep(s))",
        start_time,
        end_time,
        subset.sizes["time"],
    )
    return subset


def infer_timestep_seconds(dataset: xr.Dataset) -> int:
    time_index = pd.DatetimeIndex(pd.to_datetime(dataset["time"].values))
    if len(time_index) < 2:
        raise ValueError("Need at least two timesteps to infer the forcing timestep")

    deltas = time_index[1:] - time_index[:-1]
    first_delta = deltas[0]
    if any(delta != first_delta for delta in deltas):
        raise ValueError("Forcing timesteps are not uniform")

    time_step_seconds = int(first_delta.total_seconds())
    if time_step_seconds <= 0:
        raise ValueError(f"Invalid forcing timestep: {time_step_seconds} second(s)")

    return time_step_seconds


def pad_lookahead(dataset: xr.Dataset, lookahead_steps: int) -> xr.Dataset:
    if lookahead_steps <= 0:
        return dataset

    time_step_seconds = infer_timestep_seconds(dataset)
    last_time = pd.Timestamp(dataset["time"].values[-1])
    padding_times = pd.date_range(
        start=last_time + pd.Timedelta(seconds=time_step_seconds),
        periods=lookahead_steps,
        freq=f"{time_step_seconds}s",
    )
    if len(padding_times) == 0:
        return dataset

    LOGGER.info(
        "Padding %d lookahead timestep(s) beyond %s using the final forcing row",
        lookahead_steps,
        last_time,
    )

    last_slice = dataset.isel(time=-1)
    padding_slice = last_slice.expand_dims(time=padding_times)
    return xr.concat([dataset, padding_slice], dim="time")


def ensure_wind_components(
    dataset: xr.Dataset,
    assume_westerly_wind: bool,
) -> xr.Dataset:
    has_u = "land_surface_wind__x_component_of_velocity" in dataset.data_vars
    has_v = "land_surface_wind__y_component_of_velocity" in dataset.data_vars
    has_scalar = SCALAR_WIND_SPEED_NAME in dataset.data_vars

    if has_u and has_v:
        return dataset

    if not has_scalar:
        return dataset

    if not assume_westerly_wind:
        raise ValueError(
            "Forcing data only provides scalar wind_speed. Re-run with "
            "--assume-westerly-wind to synthesize U/V components."
        )

    LOGGER.warning(
        "Converting scalar wind speed to eastward/northward components "
        "assuming westerly flow (U=wind_speed, V=0)"
    )

    if not has_u:
        dataset["land_surface_wind__x_component_of_velocity"] = dataset[
            SCALAR_WIND_SPEED_NAME
        ].copy(deep=True)
        dataset["land_surface_wind__x_component_of_velocity"].attrs["units"] = "m/s"

    if not has_v:
        dataset["land_surface_wind__y_component_of_velocity"] = xr.zeros_like(
            dataset[SCALAR_WIND_SPEED_NAME]
        )
        dataset["land_surface_wind__y_component_of_velocity"].attrs["units"] = "m/s"

    return dataset


def validate_required_variables(dataset: xr.Dataset) -> None:
    missing = [
        variable_name
        for variable_name in REQUIRED_BMI_VARIABLES
        if variable_name not in dataset.data_vars
    ]
    if missing:
        raise ValueError(
            "Missing required forcing variables after normalization: "
            + ", ".join(sorted(missing))
        )


def infer_feature_dim(
    dataset: xr.Dataset,
    requested_feature_dim: str | None,
) -> str | None:
    if requested_feature_dim is not None:
        if requested_feature_dim not in dataset.dims:
            raise ValueError(
                f"Requested feature dimension '{requested_feature_dim}' "
                f"is not present in dataset dims {tuple(dataset.dims)}"
            )
        return requested_feature_dim

    feature_dims = [dimension for dimension in dataset.dims if dimension != "time"]
    if not feature_dims:
        return None

    for preferred_dim in PREFERRED_FEATURE_DIMS:
        if preferred_dim in feature_dims:
            return preferred_dim

    if len(feature_dims) == 1:
        return feature_dims[0]

    raise ValueError(
        "Could not infer a feature dimension from dataset dims "
        f"{tuple(dataset.dims)}. Re-run with --feature-dim."
    )


def normalize_feature_id(raw_value: Any, feature_id_prefix: str) -> str:
    if hasattr(raw_value, "item"):
        try:
            raw_value = raw_value.item()
        except ValueError:
            pass

    if isinstance(raw_value, bytes):
        raw_value = raw_value.decode("utf-8")

    feature_id = str(raw_value).strip()
    if feature_id.endswith(".0"):
        try:
            feature_id = str(int(float(feature_id)))
        except ValueError:
            pass

    if feature_id_prefix and not feature_id.startswith(feature_id_prefix):
        feature_id = f"{feature_id_prefix}{feature_id}"

    if not feature_id:
        raise ValueError("Encountered an empty feature ID")

    return feature_id


def extract_feature_ids(
    dataset: xr.Dataset,
    feature_dim: str | None,
    requested_id_variable: str | None,
    feature_id_prefix: str,
    single_feature_id: str,
) -> list[str]:
    if feature_dim is None:
        return [normalize_feature_id(single_feature_id, feature_id_prefix)]

    candidate_names: list[str] = []
    if requested_id_variable is not None:
        candidate_names.append(requested_id_variable)
    candidate_names.extend(PREFERRED_ID_NAMES)
    candidate_names.append(feature_dim)

    id_values = None
    selected_name = None
    for candidate_name in candidate_names:
        if candidate_name in dataset.data_vars:
            candidate = dataset[candidate_name]
        elif candidate_name in dataset.coords:
            candidate = dataset.coords[candidate_name]
        else:
            continue

        if candidate.dims == (feature_dim,):
            id_values = candidate.values
            selected_name = candidate_name
            break

    if id_values is None:
        raise ValueError(
            f"Could not infer feature IDs for dimension '{feature_dim}'. "
            "Re-run with --id-variable."
        )

    feature_ids = [
        normalize_feature_id(value, feature_id_prefix) for value in id_values
    ]

    if len(set(feature_ids)) != len(feature_ids):
        raise ValueError(
            f"Feature IDs derived from '{selected_name}' are not unique"
        )

    LOGGER.info(
        "Using feature IDs from '%s': %s",
        selected_name,
        ", ".join(feature_ids[:5]) + ("..." if len(feature_ids) > 5 else ""),
    )
    return feature_ids


def extract_time_series(
    dataset: xr.Dataset,
    variable_name: str,
    feature_dim: str | None,
    feature_index: int,
) -> list[Any]:
    data_array = dataset[variable_name]

    if feature_dim is not None and feature_dim in data_array.dims:
        data_array = data_array.isel({feature_dim: feature_index})

    if "time" not in data_array.dims:
        raise ValueError(f"Variable '{variable_name}' does not have a time dimension")

    extra_dims = [dimension for dimension in data_array.dims if dimension != "time"]
    if extra_dims:
        raise ValueError(
            f"Variable '{variable_name}' still has non-time dimensions {extra_dims} "
            "after feature selection"
        )

    data_array = data_array.transpose("time")
    return data_array.values.tolist()


def format_time_column(
    dataset: xr.Dataset,
    output_schema: str,
) -> list[str]:
    time_values = pd.to_datetime(dataset["time"].values)
    time_format = OUTPUT_SCHEMAS[output_schema]["time_format"]
    return pd.Series(time_values).dt.strftime(time_format).tolist()


def required_timestep_seconds(
    dataset: xr.Dataset,
    output_schema: str,
) -> int | None:
    column_specs = OUTPUT_SCHEMAS[output_schema]["columns"]
    needs_timestep = any(
        transform == RATE_TO_ACCUMULATION_TRANSFORM
        for _, _, transform in column_specs
    )
    if not needs_timestep:
        return None
    return infer_timestep_seconds(dataset)


def format_numeric_column(
    raw_values: list[Any],
    column_name: str,
    feature_id: str,
    transform: str = IDENTITY_TRANSFORM,
    time_step_seconds: int | None = None,
) -> list[str]:
    formatted_values: list[str] = []
    for row_number, raw_value in enumerate(raw_values, start=2):
        if hasattr(raw_value, "item"):
            try:
                raw_value = raw_value.item()
            except ValueError:
                pass

        try:
            numeric_value = float(raw_value)
        except (TypeError, ValueError) as exception:
            raise ValueError(
                f"Feature '{feature_id}' column '{column_name}' contains a non-numeric "
                f"value at CSV row {row_number}"
            ) from exception

        if not math.isfinite(numeric_value):
            raise ValueError(
                f"Feature '{feature_id}' column '{column_name}' contains a non-finite "
                f"value at CSV row {row_number}"
            )

        if transform == IDENTITY_TRANSFORM:
            output_value = numeric_value
        elif transform == RATE_TO_ACCUMULATION_TRANSFORM:
            if time_step_seconds is None:
                raise ValueError(
                    f"Column '{column_name}' requires a forcing timestep but none "
                    "was available"
                )
            output_value = numeric_value * time_step_seconds
        else:
            raise ValueError(f"Unsupported output transform '{transform}'")

        formatted_values.append(f"{output_value:.17g}")

    return formatted_values


def build_feature_rows(
    dataset: xr.Dataset,
    feature_dim: str | None,
    feature_index: int,
    feature_id: str,
    time_column: list[str],
    output_schema: str,
    time_step_seconds: int | None,
) -> list[list[str]]:
    numeric_columns: dict[str, list[str]] = {}
    output_columns = OUTPUT_SCHEMAS[output_schema]["columns"]
    for column_name, source_name, transform in output_columns:
        numeric_columns[column_name] = format_numeric_column(
            extract_time_series(
                dataset,
                source_name,
                feature_dim,
                feature_index,
            ),
            column_name,
            feature_id,
            transform=transform,
            time_step_seconds=time_step_seconds,
        )

    row_count = len(time_column)
    for column_name, values in numeric_columns.items():
        if len(values) != row_count:
            raise ValueError(
                f"Feature '{feature_id}' column '{column_name}' has {len(values)} "
                f"value(s), expected {row_count}"
            )

    return [
        [
            time_column[row_index],
            *(
                numeric_columns[column_name][row_index]
                for column_name, _, _ in output_columns
            ),
        ]
        for row_index in range(row_count)
    ]


def write_csv(
    csv_path: Path,
    rows: list[list[str]],
    output_schema: str,
) -> None:
    output_header = (
        "time",
        *(
            column_name
            for column_name, _, _ in OUTPUT_SCHEMAS[output_schema]["columns"]
        ),
    )
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", encoding="utf-8", newline="") as csv_stream:
        writer = csv.writer(csv_stream, lineterminator="\n")
        writer.writerow(output_header)
        writer.writerows(rows)


def write_feature_csvs(
    dataset: xr.Dataset,
    output_dir: Path,
    feature_dim: str | None,
    feature_ids: list[str],
    filename_template: str,
    output_schema: str,
) -> list[tuple[str, Path]]:
    output_dir.mkdir(parents=True, exist_ok=True)
    time_column = format_time_column(dataset, output_schema)
    time_step_seconds = required_timestep_seconds(dataset, output_schema)

    artifacts: list[tuple[str, Path]] = []
    written_paths: set[Path] = set()
    for feature_index, feature_id in enumerate(feature_ids):
        csv_name = filename_template.format(feature_id=feature_id)
        csv_path = output_dir / csv_name
        if csv_path in written_paths:
            raise ValueError(
                f"Filename template produced duplicate output path '{csv_path}'"
            )

        rows = build_feature_rows(
            dataset,
            feature_dim,
            feature_index,
            feature_id,
            time_column,
            output_schema,
            time_step_seconds,
        )
        write_csv(csv_path, rows, output_schema)

        artifacts.append((feature_id, csv_path))
        written_paths.add(csv_path)

    LOGGER.info("Wrote %d CSV forcing file(s) to %s", len(artifacts), output_dir)
    return artifacts


def write_realization(
    realization_path: Path,
    config_template: str,
    feature_ids: list[str],
) -> None:
    realization_path.parent.mkdir(parents=True, exist_ok=True)

    subdomains = {}
    for feature_id in feature_ids:
        subdomains[feature_id] = {
            "config": config_template.format(feature_id=feature_id),
        }

    realization = {"subdomains": subdomains}

    realization_path.write_text(
        json.dumps(realization, indent=2) + "\n",
        encoding="utf-8",
    )
    LOGGER.info("Wrote realization stub to %s", realization_path)


def main() -> int:
    args = parse_args()
    configure_logging(args.verbose)

    dataset = load_forcing_dataset(args.forcing_dir)
    dataset = normalize_to_bmi_variables(dataset)
    dataset = subset_to_time_window(
        dataset,
        parse_time_or_none(args.start),
        parse_time_or_none(args.end),
    )
    dataset = ensure_wind_components(dataset, args.assume_westerly_wind)
    validate_required_variables(dataset)
    dataset = pad_lookahead(dataset, args.pad_lookahead_steps)

    feature_dim = infer_feature_dim(dataset, args.feature_dim)
    feature_ids = extract_feature_ids(
        dataset,
        feature_dim,
        args.id_variable,
        args.feature_id_prefix,
        args.single_feature_id,
    )
    write_feature_csvs(
        dataset,
        args.output_dir,
        feature_dim,
        feature_ids,
        args.filename_template,
        args.output_schema,
    )

    if args.realization_path is not None:
        write_realization(
            args.realization_path,
            args.config_template,
            feature_ids,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
