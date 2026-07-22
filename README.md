# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

<a href="https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml"><img
    alt="CI"
    src="https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml/badge.svg"
    ></a>
<a href="https://tjbmontoya.com/miaow-framework/"><img
    alt="Documentation"
    src="https://img.shields.io/badge/docs-latest-blue.svg"></a>

<p align="center">
  <img src="docs/images/miaow_logo.svg" alt="MIAOW logo" width="360">
</p>

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW targets the [C++20 standard](https://en.cppreference.com/w/cpp/20.html), uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) as its actor backend, relies on [Boost](https://www.boost.org/) for process management and JSON support, and includes C++/Fortran [Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/) integration paths.

## Requirements

MIAOW requires [CMake](https://cmake.org/) 3.21+, a C++20 compiler, and a Fortran compiler. The Boost and CAF provider options specify where CMake obtains these dependencies: from packages already installed on the system, by fetching and building them from source, or by trying the system first and fetching them if necessary. See the [building and installation documentation](https://tjbmontoya.com/miaow-framework/building.html) for a description of the available provider settings.

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

Use the following commands to build MIAOW and run the `00_hello` example workflow:

```sh
cmake -S . -B build
cmake --build build --target miaow_hello
cd examples/00_hello
./miaow_hello
```

See the [Examples](https://tjbmontoya.com/miaow-framework/examples.html) section of the documentation for an overview of the available workflows and model-specific setup.

To install MIAOW for use from another CMake project, see the [library installation build](https://tjbmontoya.com/miaow-framework/building.html#library-installation-build) instructions.

## Contributing

MIAOW is an open-source project and welcomes contributions from the community. To report a bug, suggest an improvement, or discuss a contribution, please [open an issue](https://github.com/uofs-simlab/miaow-framework/issues). Code and documentation contributions should be proposed through a [pull request](https://github.com/uofs-simlab/miaow-framework/pulls).

## Contributors and funding

MIAOW was developed with funding support from the [Cooperative Institute for Research to Operations in Hydrology (CIROH)](https://ciroh.ua.edu/) and is maintained by the [Numerical Simulation Laboratory](https://github.com/uofs-simlab) at the [University of Saskatchewan](https://www.usask.ca/). See the [CONTRIBUTORS](CONTRIBUTORS.md) file for a full list of individual contributors to the project.

<p align="center">
  <a href="https://www.usask.ca/"><img
      src="docs/images/usask_logo.png"
      alt="University of Saskatchewan logo"
      height="60"></a>
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <a href="https://ciroh.ua.edu/"><img
      src="docs/images/ciroh_logo.jpg"
      alt="CIROH logo"
      height="80"></a>

</p>

## License

MIAOW is released under the [MIT License](LICENSE). Third-party components remain subject to the licences and copyright notices included with those components.
