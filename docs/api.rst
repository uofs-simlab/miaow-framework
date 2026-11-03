API reference
=============

Core runtime
------------

Identifiers and task status
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. doxygentypedef:: miaow::TaskExecutorId

.. doxygentypedef:: miaow::TaskId

.. doxygentypedef:: miaow::JobCreationId

.. doxygentypedef:: miaow::JobSubmissionId

.. doxygentypedef:: miaow::TaskMetadata

.. doxygenenum:: miaow::TaskStatus

.. doxygenfunction:: miaow::task_status_to_string

Task and job data
~~~~~~~~~~~~~~~~~

.. doxygenclass:: miaow::SimulationUnitHandle
   :members:

.. doxygenclass:: miaow::Job
   :members:

.. doxygenclass:: miaow::SimulationUnit
   :members:

.. doxygenstruct:: miaow::Task
   :members:

.. doxygenstruct:: miaow::TaskResult
   :members:

.. doxygenstruct:: miaow::UnitFailure
   :members:

.. doxygenstruct:: miaow::TaskRecord
   :members:

.. doxygenstruct:: miaow::JobRecord
   :members:

.. doxygentypedef:: miaow::JobHandle

Runtime orchestration
~~~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: miaow::TaskExecutor
   :members:

.. doxygenenum:: miaow::TaskGenerationPolicy

.. doxygenclass:: miaow::JobController
   :members:

.. doxygenclass:: miaow::TaskExecutorRegistry
   :members:

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

.. doxygenvariable:: miaow::default_bmi_time_step_tolerance_seconds

.. doxygenstruct:: miaow::StaticBmiConfig
   :members:

.. doxygenstruct:: miaow::DynamicBmiConfig
   :members:

Task executors
~~~~~~~~~~~~~~

.. doxygenclass:: miaow::StaticBmiTaskExecutor
   :members:

.. doxygenclass:: miaow::DynamicBmiTaskExecutor
   :members:

.. doxygenfunction:: miaow::bmi_subprocess_executable_path

.. doxygenfunction:: miaow::make_bmi_task(SimulationUnitHandle, const StaticBmiConfig&)

.. doxygenfunction:: miaow::make_bmi_task(SimulationUnitHandle, const DynamicBmiConfig&)

I/O and CLI helpers
-------------------

.. doxygenfunction:: miaow::print_job_summary

.. doxygenfunction:: miaow::all_tasks_succeeded

.. doxygenfunction:: miaow::ensure_expected_run_dir

.. doxygenfunction:: miaow::parse_runtime_cli_options
