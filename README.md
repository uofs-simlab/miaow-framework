# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW uses the [C++ Actor Framework (CAF)](https://www.actor-framework.org/) and interfaces with the [Basic Model Interface (BMI)](https://bmi.csdms.io/en/stable/).

## Build instructions

The following commands can be used to build `libmiaow.a` with [CMake](https://cmake.org/) and run a basic example program:

```bash
cmake -S . -B build
cmake --build build
./build/miaow_hello
```

A list of project-specific CMake flags are provided below:

| Flag | Default | Description |
| --- | --- | --- |
| `-DBUILD_EXAMPLES` | `ON` | Build the `miaow_hello` example |
| <span style="white-space:nowrap">`-DMIAOW_USE_SYSTEM_CAF`</span> | `ON` | Prefer a system-installed CAF via `find_package` |
| `-DMIAOW_FETCH_CAF` | `ON` | Fall back to fetching CAF with CMake `FetchContent` if not found by `find_package` |
| `-DMIAOW_CAF_VERSION` | `1.1.0` | Minimum CAF version for system `find_package` |
| `-DMIAOW_CAF_GIT_TAG` | `1.1.0` | CAF git tag or commit for CMake `FetchContent` |

## License 
This code is released under the [MIT license](https://opensource.org/license/mit).
