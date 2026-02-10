# MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. **MIAOW** embraces that reality by using actor-based asynchronous orchestration so tasks run when they is ready, maximizing utilization across available resources.

## Build instructions

The following commands can be used to build `libmiaow.a` and run a basic example program:
```bash
cmake -S . -B build
cmake --build build
./build/miaow_hello
```

## License 
This code is released under the [MIT license](https://opensource.org/license/mit).