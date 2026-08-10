API reference
=============

Core runtime
------------

Identifiers and task status
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. doxygentypedef:: miaow::SimulationUnitId

.. doxygentypedef:: miaow::TaskId

.. doxygentypedef:: miaow::JobId

.. doxygentypedef:: miaow::TaskMetadata

.. doxygenenum:: miaow::TaskStatus

.. doxygenfunction:: miaow::task_status_to_string

Task and job data
~~~~~~~~~~~~~~~~~

.. doxygenstruct:: miaow::Task
   :members:

.. doxygenstruct:: miaow::TaskResult
   :members:

.. doxygenstruct:: miaow::TaskRecord
   :members:

.. doxygenstruct:: miaow::JobRecord
   :members:

.. doxygentypedef:: miaow::JobHandle

Runtime orchestration
~~~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: miaow::SimulationUnit
   :members:

.. doxygenclass:: miaow::JobController
   :members:

.. doxygenclass:: miaow::SimulationUnitRegistry
   :members:

.. doxygenenum:: miaow::SubprocessLifetime

.. doxygenfunction:: miaow::run_subprocess_worker

.. doxygenstruct:: miaow::RuntimeOptions
   :members:

.. doxygenclass:: miaow::Runtime
   :members:

BMI integration
---------------

BMI configuration
~~~~~~~~~~~~~~~~~

.. doxygenenum:: miaow::DynamicBmiBackend

.. doxygenenum:: miaow::BmiDataFormat

.. doxygenvariable:: miaow::default_bmi_time_step_tolerance_seconds

.. doxygenstruct:: miaow::BmiForcingConfig
   :members:

.. doxygenstruct:: miaow::BmiOutputConfig
   :members:

.. doxygenstruct:: miaow::StaticBmiConfig
   :members:

.. doxygenstruct:: miaow::DynamicBmiConfig
   :members:

Simulation units
~~~~~~~~~~~~~~~~

.. doxygenclass:: miaow::StaticBmiSimulationUnit
   :members:

.. doxygenclass:: miaow::DynamicBmiSimulationUnit
   :members:

.. doxygenfunction:: miaow::bmi_subprocess_executable_path

.. doxygenfunction:: miaow::make_bmi_task(SimulationUnitId, const StaticBmiConfig&)

.. doxygenfunction:: miaow::make_bmi_task(SimulationUnitId, const DynamicBmiConfig&)

.. doxygenfunction:: miaow::make_bmi_tasks(SimulationUnitId, const std::vector<BmiConfig>&)

Realization workflows
---------------------

.. doxygenclass:: miaow::PartitionedBmiRealization
   :members:

.. doxygenfunction:: miaow::make_bmi_tasks(SimulationUnitId, const PartitionedBmiRealization&)

I/O and CLI helpers
-------------------

.. doxygenfunction:: miaow::print_job_summary

.. doxygenfunction:: miaow::all_tasks_succeeded

.. doxygenfunction:: miaow::ensure_expected_run_dir

.. doxygenfunction:: miaow::parse_runtime_cli_options
