# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

[![Build And Test](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml/badge.svg)](https://github.com/uofs-simlab/miaow-framework/actions/workflows/ci.yml)

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) and interfaces with the [Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/).

## Build instructions

The following commands can be used to build `libmiaow.a` with [CMake](https://cmake.org/):

```bash
cmake -S . -B build
cmake --build build
```

For convenience, we have also provided a build script `scripts/build.sh`. In either case, the library will be built in the `build` directory. Additionally, if the `BUILD_EXAMPLES` flag is set to `ON` (the default), the example programs will be placed directly in their respective subdirectories within the top-level `examples/` directory (e.g., `examples/00_hello/miaow_hello`). A complete list of project-specific CMake flags is provided below:

| Flag | Default | Description |
| --- | --- | --- |
| `-DBUILD_EXAMPLES` | `ON` | Build the `miaow_hello` and `miaow_heat` examples |
| <span style="white-space:nowrap">`-DMIAOW_USE_SYSTEM_CAF`</span> | `ON` | Prefer a system-installed CAF via `find_package` |
| `-DMIAOW_FETCH_CAF` | `ON` | Fall back to fetching CAF with CMake `FetchContent` if not found by `find_package` |
| `-DMIAOW_CAF_VERSION` | `1.1.0` | Minimum CAF version for system `find_package` |
| `-DMIAOW_CAF_GIT_TAG` | `1.1.0` | CAF git tag or commit for CMake `FetchContent` |

## Tests

Run the example-based [CTest](https://cmake.org/cmake/help/book/mastering-cmake/chapter/Testing%20With%20CMake%20and%20CTest.html) suite with:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Examples

We describe below how to run the example programs, noting that in all cases, it is assumed that we begin within the respective `examples/<xx_example_name>` subdirectory. 

1. `examples/00_hello`: A basic orchestration example using CAF, which can be run as `./miaow_hello` (or `./miaow_hello config.txt`).

2. `examples/01_heat`: Adapted from the heat equation example for the Basic Model Interface (BMI) provided by the [Community Surface Dynamics Modeling System (CSDMS)](https://csdms.colorado.edu/wiki/Main_Page). It can be run as `./miaow_heat config.txt` with the provided configuration file, and will produce the output file `bmiheatcxx.out`.

## License 
This code ~~is~~ **will probably be** released under the [MIT license](https://opensource.org/license/mit).
