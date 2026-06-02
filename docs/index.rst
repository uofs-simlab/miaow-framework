MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing
===============================================================================

Managing concurrent numerical simulation tasks with dynamic workloads and
heterogeneous hardware can feel like herding cats. **MIAOW** embraces that
reality by using actor-based asynchronous orchestration so tasks run when they
are ready, maximizing utilization across available resources.

MIAOW targets the `C++20 standard <https://en.cppreference.com/w/cpp/20.html>`_,
uses the `C++ Actor Framework (CAF) <https://www.actor-framework.org/>`_ as its
actor orchestration backend, and employs the
`Basic Model Interface (BMI) <https://bmi.csdms.io/en/stable/>`_ for Earth
system model integration.

Quick start
-----------

See the repository README for platform-specific dependency installation
commands. From the repository root, build the core library and subprocess
runner with:

.. code-block:: sh

   cmake -S . -B build -DBUILD_EXAMPLES=OFF
   cmake --build build

Run each example from its own ``examples/<example_name>`` directory:

.. code-block:: sh

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

Reference
---------

.. toctree::
   :maxdepth: 2

   api

Authors and funding
-------------------

MIAOW was developed at the University of Saskatchewan by Tristan Montoya and
Raymond Spiteri, with funding support from the `Cooperative Institute for
Research to Operations in Hydrology (CIROH) <https://ciroh.ua.edu/>`_.
