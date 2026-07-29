BMI integration
===============

MIAOW runs statically typed C++ BMI models and dynamically loaded C++ or Fortran BMI plugins through a common task interface, where each task is a single BMI model run with its own configuration, forcing, and output. This page describes the BMI integration paths and how to configure tasks for different BMI model execution patterns. See the :doc:`examples` section for concrete workflows and the :doc:`api` for constructor, member, and function details.

BMI task lifecycle
------------------

Build individual tasks with ``make_bmi_task(...)`` and task sets with ``make_bmi_tasks(...)``. Both functions translate typed BMI configuration into the generic ``Task`` representation used by the runtime.

During execution, MIAOW writes the resolved BMI configuration beside the task results as ``<task_output_name>.json`` and runs the BMI lifecycle:

1. ``Initialize(<task_output_name>.json)``
2. zero or more ``Update()`` calls
3. ``Finalize()``

Static and dynamic tasks use the same forcing, output, and artifact handling.

Task configuration options
--------------------------

Choose a setup pattern based on where the BMI model implementation comes from: a statically typed C++ model, a dynamic plugin, or a realization file.

Static BMI model
~~~~~~~~~~~~~~~~

Use this path when the concrete BMI model type is known when compiling the executable. ``StaticBmiSimulationUnit<YourModel>`` constructs one fresh model object per task.

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId heat =
       registry.add<miaow::StaticBmiSimulationUnit<BmiHeat>>();

   const miaow::StaticBmiConfig config{
       .init_config_ = heat_config,
   };
   const miaow::Task task = miaow::make_bmi_task(heat, config);

Static BMI model units always execute in the parent process.

Dynamic BMI plugin
~~~~~~~~~~~~~~~~~~

Use this path when the model is loaded from a C++ or Fortran plugin library. ``DynamicBmiConfig`` contains the plugin, model, forcing, output, and execution settings for one task.

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId dynamic_bmi =
       registry.add<miaow::DynamicBmiSimulationUnit>();

   const miaow::DynamicBmiConfig config{
       .backend_ = miaow::DynamicBmiBackend::fortran,
       .plugin_path_ = plugin_path,
       .init_config_ = init_config,
       .execution_mode_ = miaow::ExecutionMode::persistent_subprocess,
   };
   const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config);

Dynamic C++ plugins must export C linkage ``bmi_model_create()`` and ``bmi_model_destroy(...)`` symbols. Dynamic Fortran plugins must export ``register_bmi(void**)`` for the linked `NOAA-OWP iso_c_fortran_bmi bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_. See ``examples/03_dynamic_bmi`` for concrete C++ and Fortran plugin exports.

.. _realization-driven-task-creation:

NextGen-style realization-driven task creation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use this path when a `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization should produce one dynamic BMI task per feature (e.g., per catchment).

.. code-block:: cpp

   const miaow::PartitionedBmiRealization realization(realization_path);

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId dynamic_bmi =
       registry.add<miaow::DynamicBmiSimulationUnit>();

   const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(
       dynamic_bmi, realization,
       miaow::ExecutionMode::persistent_subprocess);

``PartitionedBmiRealization`` resolves the plugin, initialization, forcing, feature, and output root values. ``make_bmi_tasks(...)`` builds the dynamic task vector all at once.

Configuring dynamic task execution
----------------------------------

``StaticBmiConfig`` has no execution mode setting; the ``make_bmi_task(...)`` and ``make_bmi_tasks(...)`` functions create static BMI tasks with ``ExecutionMode::in_process``, and ``StaticBmiSimulationUnit`` does not support subprocess execution. The dynamic task builders assign execution modes as follows:

- ``make_bmi_task(simulation_unit_id, config)`` copies ``DynamicBmiConfig::execution_mode_`` to the resulting ``Task``. The field defaults to ``ExecutionMode::in_process``.
- ``make_bmi_tasks(simulation_unit_id, configs)`` builds one task per ``DynamicBmiConfig`` and preserves each configuration's execution mode. Tasks in the same vector can therefore use different modes.
- ``make_bmi_tasks(simulation_unit_id, realization, execution_mode)`` applies the supplied mode to every task generated from the realization. The argument defaults to ``ExecutionMode::in_process``.

Subprocess modes are available for dynamic BMI tasks. ``DynamicBmiConfig::subprocess_path_`` is copied to the corresponding ``Task`` and selects a non-default subprocess executable. See :ref:`task-execution-modes` for execution mode selection and runtime behaviour.

Reading forcing data
--------------------

Set ``forcing_file_`` in ``StaticBmiConfig`` or ``DynamicBmiConfig`` to enable the internal BMI forcing module. Currently, the forcing module is always appended after the model in the BMI pipeline, although it actually runs before the model's ``Update()`` calls. When no forcing is configured, it is initialized with an empty config and acts as a no-op. With configured forcing, the module reads a ``CsvFile`` entry and exposes data columns as BMI outputs. MIAOW derives explicit scalar couplings from ``variables_names_map_``, applies them immediately before model updates, and then advances the forcing source.

The variable map is interpreted as ``BMI input variable -> CSV header``. If it is empty, MIAOW derives an identity map from ``GetInputVarNames()`` and expects CSV headers with the same names as the corresponding variables. The ``output_variables_`` field controls which BMI output names are written to task CSV output; when empty, CSV output uses all names returned by ``GetOutputVarNames()``.

MIAOW's external forcing reader currently only supports CSV files. The file must define a ``time`` or ``Time`` column plus all required numeric data columns. Supported timestamp formats are ``YYYY-MM-DD HH:MM:SS`` and ``YYYY/MM/DD HH:MM:SS``. MIAOW treats the first forcing row as the model-start forcing values, so users should ensure that the first forcing row timestamp matches the model's ``start_time`` after unit normalization. Later forcing rows are streamed as the model advances; each row-to-row timestamp delta must match the receiving BMI model timestep after unit normalization. Because the only supported forcing source is CSV, the forcing provider for global `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization files should be set to ``CsvPerFeature``.

Exhausting the forcing data before the model's ``end_time`` fails the task with a ``Forcing alignment error``; forcing rows beyond ``end_time`` are silently ignored and the task completes successfully. The ``examples/02_soil_freeze_thaw`` workflow illustrates an aligned model end time and forcing series.

Diagnostics and artifacts
-------------------------

When ``RuntimeOptions::output_root_`` or CLI ``--output_root`` is set, MIAOW writes the following directory structure:

.. code-block:: text

   <output_root>/
   `-- run_<run_id>/
       `-- job_<job_id>/
           |-- job_record.json
           |-- <feature_id or task_<task_id>>.json
           `-- results/
               `-- <feature_id or task_<task_id>>.csv

Persisted run directories start at ``run_0`` and advance to the largest existing run ID plus one. MIAOW writes the resolved BMI config in the job directory and one CSV file per task under ``results/``. A non-empty ``metadata_["feature_id"]`` becomes the output file stem; otherwise MIAOW uses ``task_<task_id>``. Output stems must be unique within one job and cannot contain path separators.
