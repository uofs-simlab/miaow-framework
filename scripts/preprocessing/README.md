# MIAOW BMI forcing preprocessing

This folder contains the SYMFLUENCE-based preprocessing entrypoint used to
stage Noah-OWP runtime inputs for MIAOW.

The script runs the supported Noah-only SYMFLUENCE NGEN preprocessing steps,
selects a small deterministic catchment subset, and copies the generated Noah
inputs, forcing CSVs, parameter tables, and Fortran BMI realization into a
MIAOW runtime directory.

## Environment setup

Assume commands are run from the repository root.

Use Python 3.12 for the preprocessing environment. SYMFLUENCE `0.8.2`
supports `>=3.11,<3.14`, while newer system Python versions may be outside
that range.

```bash
/opt/homebrew/bin/python3.12 -m venv scripts/preprocessing/.venv
scripts/preprocessing/.venv/bin/pip install -r scripts/preprocessing/requirements.txt
```

Verify that `symfluence` resolves from the preprocessing venv:

```bash
scripts/preprocessing/.venv/bin/python -c "import pathlib, symfluence, sys; print(sys.prefix); print(pathlib.Path(symfluence.__file__).resolve())"
```

The printed module path should live under
`scripts/preprocessing/.venv/lib/python3.12/site-packages/`.

SYMFLUENCE expects these host libraries to be available:

- `GDAL`
- `NetCDF-C`
- `HDF5`
- `PROJ`
- `GEOS`

Useful checks:

```bash
gdalinfo --version
nc-config --version
```

If the venv cannot import `osgeo`, install GDAL Python bindings matching the
host GDAL library into the venv. For Homebrew GDAL `3.12.0`, use:

```bash
scripts/preprocessing/.venv/bin/pip install GDAL==3.12.0.post1
```

If `symfluence` import or NetCDF I/O fails with an HDF5 warning or
`NetCDF: HDF error`, rebuild `h5py` and `netCDF4` against the same Homebrew
libraries:

```bash
HDF5_DIR=/opt/homebrew/opt/hdf5 \
  scripts/preprocessing/.venv/bin/pip install --force-reinstall --no-binary=h5py h5py
USE_NCCONFIG=1 \
NETCDF4_DIR=/opt/homebrew/opt/netcdf \
HDF5_DIR=/opt/homebrew/opt/hdf5 \
  scripts/preprocessing/.venv/bin/pip install --force-reinstall --no-binary=netCDF4 netCDF4
```

## Usage

Stage the default Bow smoke subset into the Noah-OWP example runtime tree:

```bash
./scripts/preprocessing/.venv/bin/python ./scripts/preprocessing/stage_symfluence_noah_owp_inputs.py \
  --config ./scripts/preprocessing/example_symfluence_bow_02b_smoke.yaml
```

Use `--force` to rerun the selected SYMFLUENCE steps. Use `--target-dir` and
`--realization-path` to stage the generated inputs into another compatible
MIAOW Noah-OWP runtime tree.

The script writes:

- `data/configs/cat-*.input`
- `data/forcings/*_forcing.csv`
- `data/parameters/*`
- `data/realization_config.json`
- `data/catchments.geojson`

## Behaviour

- Runs the supported SYMFLUENCE NGEN preprocessing steps
- Requires `HYDROLOGICAL_MODEL: NGEN`
- Requires `NGEN_MODULES_SELECTED: NOAH`
- Requires `NGEN_MODULES_TO_CALIBRATE: NOAH`
- Selects generated `cat-*.input` files by numeric catchment ID
- Copies matching `*_forcing.csv` files and Noah parameter tables
- Writes a reduced SYMFLUENCE catchments GeoJSON for the selected catchments
- Rewrites staged Noah path fields for the target runtime directory
- Writes a relocatable copy of the NextGen-style realization format

## Limitations

- The script consumes SYMFLUENCE's generated Noah inputs and forcing CSVs
- Regenerating Bow data remains a manual preprocessing step
- The staged target must use the same runtime layout as the Noah-OWP example
