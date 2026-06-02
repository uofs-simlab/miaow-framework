# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

<a href="https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml">
  <img
    alt="CI"
    src="https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml/badge.svg">
</a>
<a href="https://tjbmontoya.com/miaow-framework/">
  <img alt="Documentation" src="https://img.shields.io/badge/docs-latest-blue.svg">
</a>

<p align="center">
  <img src="docs/images/miaow_logo.svg" alt="MIAOW logo" width="360">
</p>

Managing concurrent numerical simulation tasks with dynamic workloads and
heterogeneous hardware can feel like herding cats. **MIAOW** embraces that
reality by using actor-based asynchronous orchestration so tasks run when they
are ready, maximizing utilization across available resources.

MIAOW targets the [C++20 standard](https://en.cppreference.com/w/cpp/20.html),
uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) as its
actor backend, relies on [Boost](https://www.boost.org/) for process management
and JSON support, and includes C++/Fortran
[Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/) integration
paths.

## Requirements

| Build | Requirements |
| --- | --- |
| Core library | CMake 3.16+, C++20 compiler, Fortran compiler |
| Examples | CMake 3.21+, NetCDF C/Fortran, BLAS/LAPACK support |

CMake tries to find Boost and CAF on the system, then fetches them from source
when the provider option is `auto`. See
[Build options](docs/development.rst#build-options) for provider controls.

On Ubuntu, install core dependencies:

```sh
sudo apt-get install build-essential cmake gfortran
```

On Ubuntu, install the example dependencies:

```sh
sudo apt-get install libnetcdf-dev libnetcdff-dev libopenblas-dev
```

On macOS with [Homebrew](https://brew.sh/), install core dependencies:

```sh
sudo xcode-select --install  # if Command Line Tools are not installed
brew install cmake gcc
```

On macOS, install the example dependencies:

```sh
brew install netcdf netcdf-fortran
```

The [SUMMA](https://github.com/CH-Earth/summa) example defaults to the macOS
Accelerate framework for BLAS/LAPACK. OpenBLAS or another provider can be used
by setting `MIAOW_SUMMA_LIBRARY_LINKS`.

## Quick start

Build the core library and BMI subprocess runner:

```sh
cmake -S . -B build -DBUILD_EXAMPLES=OFF
cmake --build build
```

Build all examples and run the test suite:

```sh
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Examples

Run examples from their own `examples/<example_name>` directories:

```sh
cd examples/00_hello
./miaow_hello
cd ../01_heat
./miaow_heat
cd ../02_soil_freeze_thaw
./miaow_soil_freeze_thaw --output_root ./output --max_workers 2
cd ../03_dynamic_bmi
./miaow_dynamic_bmi --output_root ./output --max_workers 2
cd ../04_summa
./miaow_summa --realization data/realization_config.json --output_root ./output
```

The examples cover minimal task orchestration, static BMI models, dynamic C++
and Fortran BMI plugins, and a SUMMA workflow using subprocess execution. See
[Examples](docs/examples.rst) for model sources, data layout, and command-line
options.

## Documentation

Generated API documentation uses [Doxygen](https://www.doxygen.nl/) for C++
docstring extraction and [Sphinx](https://www.sphinx-doc.org/) with
[Breathe](https://www.breathe-doc.org/) for HTML rendering. Doxygen must be
installed separately and available on `PATH`.

Build the docs from a dedicated Python environment:

```sh
python3 -m venv docs/.venv
docs/.venv/bin/pip install -r docs/requirements.txt
rm -rf docs/_build/html docs/_build/doctrees
docs/.venv/bin/sphinx-build -b html -d docs/_build/doctrees docs docs/_build/html
```

The generated HTML entry point is `docs/_build/html/index.html`. The public
documentation is deployed from GitHub Actions to
[https://tjbmontoya.com/miaow-framework/](https://tjbmontoya.com/miaow-framework/).

## More information

- [Architecture](docs/architecture.rst): runtime actors, scheduling, and API shape
- [BMI integration](docs/bmi.rst): static, dynamic, pipeline, and forcing workflows
- [Examples](docs/examples.rst): example programs, data layout, and CLI options
- [Development notes](docs/development.rst): build options, Conan setup, and layout
- [API reference](docs/api.rst): generated public API documentation

## Current limitations

- Distributed-memory parallelism is not currently supported.
- Target workloads are embarrassingly parallel tasks without inter-task communication.
- Windows is not supported, and there is currently no plan to add such support.

## Authors and funding

MIAOW was developed at the University of Saskatchewan by Tristan Montoya and
Raymond Spiteri, with funding support from the
[Cooperative Institute for Research to Operations in Hydrology
(CIROH)](https://ciroh.ua.edu/).

<p align="center">
  <a href="https://www.usask.ca/">
    <img
      src="docs/images/usask_logo.png"
      alt="University of Saskatchewan logo"
      height="60">
  </a>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp
  <a href="https://ciroh.ua.edu/">
    <img
      src="docs/images/ciroh_logo.jpg"
      alt="CIROH logo"
      height="80">
  </a>

</p>
