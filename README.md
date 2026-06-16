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
| Core library | [CMake](https://cmake.org/) 3.16+, C++20 compiler, Fortran compiler |
| Examples | CMake 3.21+, [NetCDF](https://www.unidata.ucar.edu/software/netcdf/) C/Fortran, BLAS/LAPACK support |

CMake tries to find Boost and CAF on the system, then fetches them from source
when the provider option is `auto`. See the
[Development](https://tjbmontoya.com/miaow-framework/development.html) section
in the [documentation](https://tjbmontoya.com/miaow-framework/index.html) for a
description of the provider options.

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
sudo xcode-select --install  # if Apple's Command Line Tools are not installed
brew install cmake gcc
```

On macOS with Homebrew, install the example dependencies. By default, Apple's
built-in [Accelerate framework](https://developer.apple.com/documentation/accelerate) is used for BLAS/LAPACK, so only NetCDF is
needed:

```sh
brew install netcdf netcdf-fortran
```

## Quick start

Build the core library and subprocess runner without examples or tests:

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
and Fortran BMI plugins, and a [SUMMA](https://github.com/CH-Earth/summa)
workflow. See the [Examples](https://tjbmontoya.com/miaow-framework/examples.html)
page for model sources, data layout, and command line options.

The `examples/05_libtrixi_bmi` example is optional because it depends on the
external [`libtrixi-bmi`](https://github.com/tristanmontoya/libtrixi-bmi)
package, an installed libtrixi prefix, and a libtrixi Julia project:

```sh
cmake -S . -B build \
  -DMIAOW_EXAMPLES=05_libtrixi_bmi \
  -DLIBTRIXI_PREFIX=<libtrixi-install-prefix> \
  -DLIBTRIXI_PROJECT_DIR=<libtrixi-julia-project> \
  -DLIBTRIXI_JULIA_DEPOT_PATH=<julia-depot-path>
cmake --build build --target miaow_libtrixi_bmi
```

By default, CMake resolves `libtrixi-bmi` under `third_party/libtrixi-bmi` and
fetches it if it is missing. Set `MIAOW_LIBTRIXI_BMI_ROOT` to use a local
checkout instead.

`LIBTRIXI_JULIA_DEPOT_PATH` is optional; it is used as a `JULIA_DEPOT_PATH`
fallback when the runtime environment does not already set one.

## Authors and funding

MIAOW was developed at the University of Saskatchewan by
[Tristan Montoya](https://tjbmontoya.com/) and
[Raymond Spiteri](https://www.cs.usask.ca/people/faculty%20profiles/ray-spiteri.php),
with funding support from the
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
