BMI integration
===============

MIAOW runs statically typed C++ BMI models and dynamically loaded C++ or Fortran BMI plugins through a common task interface, where each task is a single BMI model run with its own configuration, forcing, and output. This page describes the BMI integration paths and how to configure tasks for different BMI model execution patterns. See the :doc:`examples` section for concrete workflows and the :doc:`api` for constructor, member, and function details.

BMI task lifecycle
------------------

Build individual tasks with ``make_bmi_task(...)`` and task sets with ``make_bmi_tasks(...)``. Both functions translate typed BMI configuration into the generic ``Task`` representation that MIAOW passes to simulation unit actors for execution. During task execution, ``BmiPipeline`` initializes directly from the ``BmiPipelineConfig`` stored in ``Task::args_``. When runtime output is enabled, MIAOW also writes the resolved pipeline configuration beside the task results as ``<task_output_name>.json``. Task execution does not require this file; it is an optional pipeline configuration artifact that can later be passed to ``BmiPipeline::Initialize(std::string)`` for file-based initialization, for example, to reproduce a previous run. ``PartitionedBmiRealization`` reads source NextGen-style realization files, while ``BmiPipeline::Initialize(std::string)`` reads artifacts that use MIAOW's resolved pipeline schema.

After loading the pipeline configuration, ``BmiPipeline`` runs the BMI lifecycle for its components, which currently include a single BMI model and an optional internal forcing module. The pipeline lifecycle, which is implemented in `src/bmi/execute.cpp <https://github.com/uofs-simlab/miaow-framework/blob/main/src/bmi/execute.cpp>`_, consists of three phases:

1. Call ``Initialize()`` on every component (including the forcing module if configured) with the component's ``init_config_``.
2. Perform zero or more complete pipeline ``Update()`` calls, which involve applying the current forcing values, advancing the model, writing output if configured, and advancing the forcing source.
3. ``Finalize()`` every initialized component.

Task configuration options
--------------------------

Choose a setup pattern based on where the BMI model implementation comes from: a statically typed C++ model, a dynamic plugin, or a realization file.

Static BMI model
~~~~~~~~~~~~~~~~

Use this path when the concrete BMI model type is known when compiling the executable. ``StaticBmiSimulationUnit<YourModel>`` constructs one new model object per task.

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId heat =
       registry.add<miaow::StaticBmiSimulationUnit<BmiHeat>>();

   const miaow::StaticBmiConfig config{
       .init_config_ = heat_config,
   };
   const miaow::Task task = miaow::make_bmi_task(heat, config);

Static BMI model units always execute in the parent process, rather than in a subprocess.

Dynamic BMI plugin
~~~~~~~~~~~~~~~~~~

Use this path when the model is loaded from a C++ or Fortran plugin library. ``DynamicBmiConfig`` contains the plugin, model, forcing, and output settings for one task.

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId dynamic_bmi =
       registry.add<miaow::DynamicBmiSimulationUnit>();

   const miaow::DynamicBmiConfig config{
       .backend_ = miaow::DynamicBmiBackend::fortran,
       .plugin_path_ = plugin_path,
       .init_config_ = init_config,
   };
   const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config);

Dynamic C++ plugins must export C linkage ``bmi_model_create()`` and ``bmi_model_destroy(...)`` symbols. Dynamic Fortran plugins must export ``register_bmi(void**)`` for the linked `NOAA-OWP iso_c_fortran_bmi bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_. See ``examples/03_dynamic_bmi`` for concrete C++ and Fortran plugin exports.

.. _realization-driven-task-creation:

NextGen-style realization-driven task creation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use this path when a `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization file should produce one dynamic BMI task per feature (e.g., per catchment).

.. code-block:: cpp

   const miaow::PartitionedBmiRealization realization(realization_path);

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId bmi_subprocess =
       registry.add_subprocess(
           miaow::bmi_subprocess_executable_path(),
           miaow::SubprocessLifetime::persistent);

   const std::vector<miaow::Task> tasks =
       miaow::make_bmi_tasks(bmi_subprocess, realization);

``PartitionedBmiRealization`` resolves relative plugin, initialization, and forcing paths against the directory containing the realization file and reads feature IDs from the sibling ``catchments.geojson``. ``make_bmi_tasks(...)`` builds the dynamic task vector all at once. Only a subset of the NextGen realization configuration is currently supported by MIAOW, with the following restrictions:

- ``global.formulations`` contains exactly one ``bmi_multi`` entry.
- The ``bmi_multi`` formulation contains exactly one ``bmi_fortran`` module.
- Global forcing uses the ``CsvPerFeature`` provider with ``path`` and ``file_pattern`` values.
- A sibling ``catchments.geojson`` provides feature IDs, and both module ``init_config`` and forcing ``file_pattern`` contain ``{{id}}``.
- The module defines ``library_file`` and ``init_config`` and sets ``uses_forcing_file`` to ``false``. An optional ``variables_names_map`` maps BMI input variables to forcing variables.
- An optional ``output_variables`` array belongs to the containing ``bmi_multi`` parameters. An omitted or empty array selects all model outputs, and an optional ``time.output_interval`` configures the output cadence.

MIAOW currently assumes that realization-driven models follow the `NextGen BMI time convention <https://noaa-owp.github.io/ngen/md_doc_2_b_m_iconventions.html>`_ and report model time in seconds.

Selecting BMI task execution
----------------------------

The ``SimulationUnitId`` passed to ``make_bmi_task(...)`` or ``make_bmi_tasks(...)`` selects execution. Register ``DynamicBmiSimulationUnit`` for local plugin execution, or register an executable with ``SimulationUnitRegistry::add_subprocess(...)`` for isolated execution. ``DynamicBmiConfig`` and the resulting ``Task`` contain only BMI workload configuration.

``bmi_subprocess_executable_path()`` resolves the bundled worker that constructs and executes ``BmiPipeline`` in the child. ``SubprocessLifetime::single_task`` starts one child for every task, while ``SubprocessLifetime::persistent`` retains each successful child for later tasks handled by the same job worker. Applications can instead register another executable that implements the public ``Task`` to ``TaskResult`` subprocess contract.

``StaticBmiSimulationUnit`` constructs its linked model in the parent process. A subprocess can handle a task built from ``StaticBmiConfig`` only when the registered executable supplies the corresponding model implementation itself.

Reading forcing data
--------------------

Set the ``forcing_`` field in ``StaticBmiConfig`` or ``DynamicBmiConfig`` to a ``BmiForcingConfig`` to enable MIAOW-managed forcing; an absent value adds no MIAOW forcing module and delegates forcing to the model. With configured forcing, MIAOW appends the internal forcing module (which currently supports per-task CSV files only as its backend) after the model and exposes the file's data columns as BMI outputs. MIAOW derives explicit scalar couplings from ``BmiForcingConfig::variables_names_map_``, applies the current forcing values immediately before each model update, and then advances the forcing source after the model update.

The variable map is interpreted as ``BMI input variable -> CSV header``. If it is empty, MIAOW derives an identity map from ``GetInputVarNames()`` and expects CSV headers with the same names as the corresponding variables. Every configured key must identify a BMI input variable; general output aliasing and aliases for model-to-model coupling are not supported.

The forcing file must define a ``time`` or ``Time`` column plus all required numeric data columns. Supported timestamp formats are ``YYYY-MM-DD HH:MM:SS`` and ``YYYY/MM/DD HH:MM:SS``. MIAOW treats the first forcing row as the model-start forcing values, so users should ensure that the first forcing row timestamp matches the model's ``start_time`` after unit normalization. Later forcing rows are streamed as the model advances; each row-to-row timestamp delta must match the receiving BMI model timestep after unit normalization. Because the only supported forcing source is CSV, the forcing provider for global `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization files should be set to ``CsvPerFeature``.

Exhausting the forcing data before the model's ``end_time`` fails the task with a ``Forcing alignment error``; forcing rows beyond ``end_time`` are silently ignored and the task completes successfully. The ``examples/02_soil_freeze_thaw`` workflow illustrates an aligned model end time and forcing series.

Writing BMI output
------------------

Set the ``output_`` field in ``StaticBmiConfig`` or ``DynamicBmiConfig`` to a ``BmiOutputConfig`` to enable MIAOW-managed task output, which currently supports CSV files only. An absent output configuration disables the MIAOW-managed task artifact and delegates output to the model. An enabled configuration with an empty ``BmiOutputConfig::variables_`` list writes every variable returned by ``GetOutputVarNames()``; a non-empty list writes the selected variables in their configured order. The CSV writer supports BMI variables reported as ``int``, ``float``, or ``double`` and fails the task when a selected variable has another type.

``BmiOutputConfig::interval_`` controls the output cadence in the units returned by the selected output model's ``GetTimeUnits()``. An absent interval writes the initialized state and every completed pipeline update. A present interval must be finite and positive; it writes the initialized state, each reachable model-time boundary, and the final state. Boundaries later than the model end time are not attempted, and a boundary within ``time_step_tolerance_`` of the end time shares one final row.

The ``time_step_tolerance_`` field in the BMI configuration defines the absolute tolerance for output-boundary and final-time comparisons. The default is ``1e-4`` in the selected output model's time unit. The value must be finite, non-negative, and less than half of ``GetTimeStep()``. For each interval between outputs, ``BmiPipeline::UpdateUntil()`` calls ``Update()`` repeatedly until the output model time is within tolerance of the requested boundary. If the update overshoots the requested boundary by more than the configured tolerance, the task fails. As an example, the following configuration writes a single CSV file with the ``streamflow`` output variable every 24 model-time units:

.. code-block:: cpp

   const miaow::DynamicBmiConfig config{
       .backend_ = miaow::DynamicBmiBackend::fortran,
       .plugin_path_ = plugin_path,
       .output_ = miaow::BmiOutputConfig{
           .variables_ = {"streamflow"},
           .interval_ = 24.0,
       },
   };

Writing a BMI output CSV requires a non-empty ``RuntimeOptions::output_root_`` or CLI ``--output_root`` value. When the output root is empty, MIAOW runs the tasks without writing runtime-managed files, including BMI output CSVs, resolved pipeline configuration JSON, job metadata, and subprocess logs. A configured output root still stores non-CSV artifacts when BMI output is disabled.

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

Persisted run directories start at ``run_0`` and advance to the largest existing run ID plus one. MIAOW writes the resolved BMI config in the job directory and, when BMI output is enabled, one CSV file per task under ``results/``. A non-empty ``metadata_["feature_id"]`` becomes the output file stem; otherwise MIAOW uses ``task_<task_id>``. Output stems must be unique within one job and cannot contain path separators.
