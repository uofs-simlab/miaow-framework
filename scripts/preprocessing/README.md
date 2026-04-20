# MIAOW BMI forcing preprocessing

This folder contains a narrow converter for model-ready NetCDF forcing files
that have already been remapped to basin or catchment averages.

The script writes one CSV per feature for MIAOW's BMI forcing path and can
optionally emit a config-only `realization.json` stub for MIAOW examples that
construct one task per subdomain.

It supports two output schemas:
- `bmi`: canonical BMI column names for framework-driven forcing
- `native-aorc`: native AORC-style columns matching files such as
  `cat87_01Dec2015.csv`

## Environment setup

Assume commands are run from the repository root.

```bash
python3 -m venv scripts/preprocessing/.venv
scripts/preprocessing/.venv/bin/pip install -r scripts/preprocessing/requirements.txt
```

## Usage

Convert model-ready NetCDF forcing into canonical BMI-style CSV headers:

```bash
./scripts/preprocessing/.venv/bin/python ./scripts/preprocessing/netcdf_to_per_feature_csv.py \
  /path/to/model_ready_forcing \
  --output-dir /tmp/miaow_preprocessed_forcing \
  --realization-path /tmp/miaow_preprocessed_forcing/realization.json
```

Convert the same forcing into native AORC-style CSVs for model-owned parsers:

```bash
./scripts/preprocessing/.venv/bin/python ./scripts/preprocessing/netcdf_to_per_feature_csv.py \
  /path/to/model_ready_forcing \
  --output-dir /tmp/miaow_preprocessed_native_forcing \
  --output-schema native-aorc
```

## Variable translation table

`netcdf_to_per_feature_csv.py` mirrors the broader forcing translation used in
SYMFLUENCE. In SYMFLUENCE, the authoritative implementations live in
`symfluence.data.utils.variable_utils.VariableStandardizer`,
`symfluence.data.preprocessing.dataset_handlers.*`, and
`symfluence.models.ngen.preprocessor._write_csv_forcing_files()`.

The table below records the input-name aliases that this script recognizes
before normalizing them to canonical internal BMI variables.

| Physical quantity | AORC | NextGen forcing | SUMMA | BMI CSV header |
| --- | --- | --- | --- | --- |
| Time | `time` | `Time` | `time` | `time` |
| Precipitation rate | `APCP_surface` | `RAINRATE` | `pptrate` | `atmosphere_water__liquid_equivalent_precipitation_rate` |
| Air temperature | `TMP_2maboveground` | `T2D` | `airtemp` | `land_surface_air__temperature` |
| Specific humidity | `SPFH_2maboveground` | `Q2D` | `spechum` | `atmosphere_air_water~vapor__specific_humidity` |
| Surface pressure | `PRES_surface` | `PSFC` | `airpres` | `land_surface_air__pressure` |
| Downwelling shortwave radiation | `DSWRF_surface` | `SWDOWN` | `SWRadAtm` | `land_surface_radiation~incoming~shortwave__energy_flux` |
| Downwelling longwave radiation | `DLWRF_surface` | `LWDOWN` | `LWRadAtm` | `land_surface_radiation~incoming~longwave__energy_flux` |
| Eastward wind component | `UGRD_10maboveground` | `U2D` | `windspd_u` | `land_surface_wind__x_component_of_velocity` |
| Northward wind component | `VGRD_10maboveground` | `V2D` | `windspd_v` | `land_surface_wind__y_component_of_velocity` |
| Scalar wind speed | derived from vector wind if needed | no direct standard column | `windspd` | no direct BMI header; the script synthesizes x/y components as `U=wind_speed`, `V=0` when only scalar wind is available |

Notes:
- The current implementation accepts the SUMMA, AORC-style, and NWM/NextGen
  shorthand variable names shown above when the data is already in model-ready
  units.
- `--output-schema bmi` writes canonical BMI column names.
- `--output-schema native-aorc` writes
  `time,APCP_surface,DLWRF_surface,DSWRF_surface,PRES_surface,SPFH_2maboveground,TMP_2maboveground,UGRD_10maboveground,VGRD_10maboveground,precip_rate`.
- In `native-aorc` mode, `APCP_surface` is derived as the per-timestep
  accumulation from the internal precipitation rate, while `precip_rate`
  remains the rate value.
- Raw AORC precipitation and provider-specific forcing products may require
  unit or rate conversion before they match the model-ready SUMMA/NextGen
  columns listed here.

## Behaviour

- Reads all `*.nc` files in the input directory and concatenates them along
  `time`
- Normalizes SUMMA, AORC-style, and NWM/NextGen shorthand forcing names into
  BMI forcing variable names
- Synthesizes wind components from scalar `windspd` as `U=wind_speed`, `V=0`
  unless `--no-assume-westerly-wind` is given
- Detects feature IDs from `hruId`, `ids`, `feature_id`, or the feature
  coordinate itself
- Writes one CSV per feature using `{feature_id}.csv` by default in either
  `bmi` or `native-aorc` format
- Optionally writes a config-only `realization.json` stub keyed by feature ID

## Limitations

- Input data must already be spatially remapped and time-aligned
- The input timestep must be uniform
- The current converter expects the Noah-OWP forcing set:
  precipitation, temperature, humidity, pressure, shortwave, longwave, and
  wind
