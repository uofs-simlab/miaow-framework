API reference
=============

Core runtime
------------

Identifiers and task status
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. doxygentypedef:: miaow::SimulationUnitId
   :project: MIAOW

.. doxygentypedef:: miaow::TaskId
   :project: MIAOW

.. doxygentypedef:: miaow::JobId
   :project: MIAOW

.. doxygentypedef:: miaow::TaskMetadata
   :project: MIAOW

.. doxygenenum:: miaow::TaskStatus
   :project: MIAOW

.. doxygenenum:: miaow::ExecutionMode
   :project: MIAOW

.. doxygenfunction:: miaow::task_status_to_string
   :project: MIAOW

Task and job data
~~~~~~~~~~~~~~~~~

.. doxygenstruct:: miaow::Task
   :project: MIAOW
   :members:

.. doxygenstruct:: miaow::TaskResult
   :project: MIAOW
   :members:

.. doxygenstruct:: miaow::TaskRecord
   :project: MIAOW
   :members:

.. doxygenstruct:: miaow::JobRecord
   :project: MIAOW
   :members:

Runtime orchestration
~~~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: miaow::ExecutionContext
   :project: MIAOW
   :members:

.. doxygenclass:: miaow::SimulationUnit
   :project: MIAOW
   :members:

.. doxygentypedef:: miaow::SimulationUnitFactory
   :project: MIAOW

.. doxygenclass:: miaow::SimulationUnitRegistry
   :project: MIAOW
   :members:

.. doxygenstruct:: miaow::RuntimeOptions
   :project: MIAOW
   :members:

.. doxygenclass:: miaow::Runtime
   :project: MIAOW
   :members:

BMI integration
---------------

MIAOW's BMI support targets the
`Basic Model Interface <https://bmi.csdms.io/en/stable/>`_ and the
`CSDMS BMI C++ interface <https://github.com/csdms/bmi-cxx>`_.
Dynamic Fortran plugins use the
`NOAA-OWP BMI bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_.
Realization-driven workflows follow the
`NextGen <https://github.com/NOAA-OWP/ngen>`_ realization shape.

BMI configuration
~~~~~~~~~~~~~~~~~

.. doxygenenum:: miaow::DynamicBmiBackend
   :project: MIAOW

.. doxygenstruct:: miaow::StaticBmiConfig
   :project: MIAOW
   :members:

.. doxygenstruct:: miaow::DynamicBmiConfig
   :project: MIAOW
   :members:

Simulation units
~~~~~~~~~~~~~~~~

.. doxygenclass:: miaow::StaticBmiSimulationUnit
   :project: MIAOW
   :members:

.. doxygenclass:: miaow::DynamicBmiSimulationUnit
   :project: MIAOW
   :members:

.. doxygenfunction:: miaow::make_bmi_task(SimulationUnitId, const StaticBmiConfig&)
   :project: MIAOW

.. doxygenfunction:: miaow::make_bmi_task(SimulationUnitId, const DynamicBmiConfig&)
   :project: MIAOW

.. doxygenfunction:: miaow::make_bmi_tasks(SimulationUnitId, const std::vector<BmiConfig>&)
   :project: MIAOW

Realization workflows
---------------------

.. doxygenclass:: miaow::PartitionedBmiRealization
   :project: MIAOW
   :members:

.. doxygenfunction:: miaow::make_bmi_tasks(SimulationUnitId, const PartitionedBmiRealization&, ExecutionMode)
   :project: MIAOW

I/O and CLI helpers
-------------------

.. doxygenfunction:: miaow::print_diagnostics
   :project: MIAOW

.. doxygenfunction:: miaow::print_task_results
   :project: MIAOW

.. doxygenfunction:: miaow::all_tasks_succeeded
   :project: MIAOW

.. doxygenfunction:: miaow::ensure_expected_run_dir
   :project: MIAOW

.. doxygenfunction:: miaow::parse_runtime_cli_options
   :project: MIAOW

.. doxygenfunction:: miaow::write_repeated_hourly_forcing_csv
   :project: MIAOW

.. doxygenfunction:: miaow::make_forcing_config
   :project: MIAOW
