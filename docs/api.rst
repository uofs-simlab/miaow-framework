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

BMI model types
~~~~~~~~~~~~~~~

.. doxygenenum:: miaow::DynamicBmiBackend
   :project: MIAOW

.. doxygenclass:: miaow::DynamicBmiModel
   :project: MIAOW
   :members:

.. doxygenclass:: miaow::BmiPipeline
   :project: MIAOW
   :members:

Simulation units
~~~~~~~~~~~~~~~~

Use ``BmiSimulationUnit<BmiPipeline>`` when the task argument stores a
serialized ``BmiPipelineConfig`` and the pipeline should build its own model
modules, including dynamic plugin modules and subprocess tasks. Use
``BmiSimulationUnit<YourModel>`` when a linked model instance should be
supplied directly to the task-level pipeline in the parent process.

.. doxygenstruct:: miaow::BmiPipelineConfig
   :project: MIAOW
   :members:

.. doxygenfunction:: miaow::make_bmi_model_config
   :project: MIAOW

.. doxygenfunction:: miaow::bmi_pipeline_config_to_value
   :project: MIAOW

.. doxygenstruct:: miaow::BmiModuleConfig
   :project: MIAOW
   :members:

.. doxygenclass:: miaow::BmiSimulationUnit
   :project: MIAOW
   :members:

Realization workflows
---------------------

.. doxygenstruct:: miaow::PartitionedBmiRealization
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
