API
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

.. doxygenenum:: miaow::TaskStatus
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

.. doxygenstruct:: miaow::JobDiagnostics
   :project: MIAOW
   :members:

.. doxygenstruct:: miaow::JobResult
   :project: MIAOW
   :members:

Runtime orchestration
~~~~~~~~~~~~~~~~~~~~~

.. doxygenclass:: miaow::SimulationUnit
   :project: MIAOW
   :members:

.. doxygentypedef:: miaow::SimulationUnitExecutor
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

Dynamic BMI types
~~~~~~~~~~~~~~~~~

.. doxygenenum:: miaow::DynamicBmiBackend
   :project: MIAOW

.. doxygenenum:: miaow::BmiExecutionMode
   :project: MIAOW

.. doxygentypedef:: miaow::DynamicCppBmi
   :project: MIAOW

.. doxygentypedef:: miaow::DynamicFortranBmi
   :project: MIAOW

.. doxygenclass:: miaow::DynamicBmi< DynamicBmiBackend::cpp >
   :project: MIAOW
   :members:

.. doxygenclass:: miaow::DynamicBmi< DynamicBmiBackend::fortran >
   :project: MIAOW
   :members:

Simulation units
~~~~~~~~~~~~~~~~

.. doxygenfunction:: miaow::execute_bmi_model
   :project: MIAOW

.. doxygenclass:: miaow::BmiSimulationUnit
   :project: MIAOW
   :members:

.. doxygenclass:: miaow::BmiSimulationUnit< DynamicBmi< Backend > >
   :project: MIAOW
   :members:

.. doxygenfunction:: miaow::execute_dynamic_bmi_model_in_subprocess
   :project: MIAOW

Realization workflows
---------------------

.. doxygenstruct:: miaow::RealizationFeature
   :project: MIAOW
   :members:

.. doxygenstruct:: miaow::RealizationConfig
   :project: MIAOW
   :members:

.. doxygenfunction:: miaow::build_bmi_realization_tasks
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
