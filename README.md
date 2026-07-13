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

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW targets the [C++20 standard](https://en.cppreference.com/w/cpp/20.html), uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) as its actor backend, relies on [Boost](https://www.boost.org/) for process management and JSON support, and includes C++/Fortran [Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/) integration paths.

## Requirements

MIAOW requires [CMake](https://cmake.org/) 3.21+, a C++20 compiler, and a Fortran compiler. CMake tries to find Boost and CAF on the system, then fetches them from source when the provider option is `auto`. See the [Development](https://tjbmontoya.com/miaow-framework/development.html) section in the [documentation](https://tjbmontoya.com/miaow-framework/index.html) for a description of the provider options.

On Ubuntu:

```sh
sudo apt-get install build-essential cmake gfortran
```

On macOS with [Homebrew](https://brew.sh/):

```sh
sudo xcode-select --install  # if Apple's Command Line Tools are not installed
brew install cmake gcc
```

## Quick start

Configure MIAOW, build the Hello World example, and run it:

```sh
cmake -S . -B build
cmake --build build --target miaow_hello
cd examples/00_hello
./miaow_hello
```

See the [development documentation](https://tjbmontoya.com/miaow-framework/development.html) for requirements, build options, and testing. See the [examples documentation](https://tjbmontoya.com/miaow-framework/examples.html) for the available workflows and model-specific setup.

## Documentation

The full documentation covers the [architecture](https://tjbmontoya.com/miaow-framework/architecture.html), [BMI integration](https://tjbmontoya.com/miaow-framework/bmi.html), [examples](https://tjbmontoya.com/miaow-framework/examples.html), [development workflow](https://tjbmontoya.com/miaow-framework/development.html), and [API reference](https://tjbmontoya.com/miaow-framework/api.html).

## Authors and funding

MIAOW was developed at the University of Saskatchewan with funding support from the [Cooperative Institute for Research to Operations in Hydrology (CIROH)](https://ciroh.ua.edu/).

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
