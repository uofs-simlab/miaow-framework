BMI integration
=======================================

Use ``BmiSimulationUnit<BmiModelType>`` from ``include/miaow_bmi.hpp`` to run
BMI-compatible models. The simulation unit creates one task-level model or
pipeline, runs the BMI lifecycle, writes artifacts, and returns a
``TaskResult``.

There are three common ways to use it:

- ``BmiSimulationUnit<YourModel>`` runs one linked BMI model per task, where
  ``YourModel`` is a subtype of ``miaow::bmi_api::Bmi`` defined in the code.
- ``BmiSimulationUnit<DynamicBmiModel>`` runs one BMI model loaded from a
  dynamic plugin per task.
- ``BmiSimulationUnit<BmiPipeline>`` runs a complete pipeline described by the
  task config, which may be obtained from partitioning a NextGen-style
  realization file.

Each BMI task starts from one typed ``BmiPipelineConfig``, converted to a
``caf::config_value`` before being stored in the task:

.. code-block:: cpp

   task.args_[0] = miaow::bmi_pipeline_config_to_value(pipeline_config);

During execution, MIAOW writes that config beside the task results as
``<task_output_name>.json``. The pipeline uses that file for the BMI lifecycle:

1. ``Initialize(<task_output_name>.json)``
2. zero or more ``Update()`` calls
3. ``Finalize()``

Within those calls, the pipeline calls the corresponding BMI methods for each
module in order. Single-model runs contain one module.

Executing a static BMI model
----------------------------

Use ``BmiSimulationUnit<YourModel>`` when the BMI model class is linked into
the executable. The simulation unit constructs one model object per task. The
task config comes from ``make_bmi_model_config(...)``, which records the model
initialization file plus optional forcing and variable mapping.

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId heat =
       registry.add<miaow::BmiSimulationUnit<BmiHeat>>();

   const miaow::BmiPipelineConfig pipeline_config =
       miaow::make_bmi_model_config(heat_config);
   const std::vector<miaow::Task> tasks{
       {heat, miaow::bmi_pipeline_config_to_value(pipeline_config)}};

At execution time, MIAOW wraps the model in ``BmiPipeline`` so the same
lifecycle, forcing, output writing, and artifact tracking are used for static
and dynamic BMI work.

Executing a dynamic BMI model
-----------------------------

Use ``BmiSimulationUnit<DynamicBmiModel>`` when the task should run one BMI
model loaded from a plugin library. ``BmiModuleConfig`` describes how to load
the plugin. The task config still comes from ``make_bmi_model_config(...)``
because the task runs one model. The dynamic model is configured when
registered; the task config supplies the per-task initialization and forcing
paths.

.. code-block:: cpp

   const miaow::BmiModuleConfig module_config(
       "bmi_fortran", miaow::DynamicBmiBackend::fortran, plugin_path);

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId model =
       registry.add<miaow::BmiSimulationUnit<miaow::DynamicBmiModel>>(
           module_config);

   const miaow::BmiPipelineConfig pipeline_config =
       miaow::make_bmi_model_config(init_config);
   const std::vector<miaow::Task> tasks{
       {model, miaow::bmi_pipeline_config_to_value(pipeline_config)}};

Use the C++ backend for plugins that export ``bmi_model_create()`` and
``bmi_model_destroy(...)``. Use the Fortran backend for plugins that export
``register_bmi(void**)`` for the linked procedural BMI bridge.

Executing a realization-driven BMI pipeline
-------------------------------------------

Use ``BmiSimulationUnit<BmiPipeline>`` when the task's ``BmiPipelineConfig``
describes the model modules directly. This is the path used by realization
workflows and by pipelines with more than one model module.

.. code-block:: cpp

   miaow::PartitionedBmiRealization realization(realization_path);

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId pipeline =
       registry.add<miaow::BmiSimulationUnit<miaow::BmiPipeline>>(
           miaow::BmiExecutionMode::subprocess);

   const std::vector<miaow::Task> tasks =
       miaow::build_bmi_realization_tasks(realization, pipeline);

``PartitionedBmiRealization`` reads a NextGen-style realization and produces
one ``BmiPipelineConfig`` per feature. ``build_bmi_realization_tasks(...)``
turns those feature configs into MIAOW tasks for the registered pipeline unit.

Subprocess mode runs one BMI lifecycle in a child process, which is useful for
BMI models that are not safe to run concurrently in one process. The SUMMA
example uses subprocess mode.

For hand-built dynamic pipelines, construct a ``BmiPipelineConfig`` from a
vector of ``BmiModuleConfig`` entries. Each module config carries the module
name, backend, plugin path, initialization config, optional variable map, and
optional CSV output variable list.

Reading forcing data
--------------------

Forcing is represented in the pipeline config as ``global.forcing``, then
normalized into an internal BMI forcing module. The forcing module is always
appended after the realization module; when no forcing is configured, it is
initialized with an empty config and acts as a no-op. With configured forcing,
the module reads a CSV file and exposes data columns as BMI outputs.
``BmiPipeline`` derives explicit scalar couplings from each module's
``variables_names_map``, applies those couplings immediately before the target
model updates, and advances the forcing source later in the same ordered update
loop.

The BMI pipeline config forcing object uses the following fields:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Key
     - Purpose
   * - ``provider``
     - ``CsvFile`` for resolved task-local forcing
   * - ``file``
     - Path to the CSV forcing file

Each BMI module may include ``variables_names_map`` in its ``params`` object.
The map is interpreted as ``BMI input variable -> CSV header``.

If ``variables_names_map`` is omitted, MIAOW derives a model-authoritative
identity map from ``GetInputVarNames()`` and expects same-named CSV headers.
Explicit map entries override that identity behaviour completely.

Each BMI module may also include ``output_variables``, an ordered list of BMI
output names written to task CSV output. When omitted, CSV output uses all
names returned by ``GetOutputVarNames()``.

MIAOW's external forcing reader currently only supports CSV files. The file must define a
``time`` or ``Time`` column plus all required numeric data columns. Supported
timestamp formats are ``YYYY-MM-DD HH:MM:SS`` and ``YYYY/MM/DD HH:MM:SS``.
MIAOW treats the first forcing row as the model-start forcing values. Later
forcing rows are streamed as the model advances; each row-to-row timestamp
delta must match the receiving BMI model timestep after unit normalization.

Exhausting the forcing data before the model's ``end_time`` fails the task
with a ``Forcing alignment error``; forcing rows beyond ``end_time`` are
silently ignored. In ``examples/02_soil_freeze_thaw``, three tasks exercise
matched and mismatched combinations of model end time and forcing series
length: a matched 512-day run, a 512-day run whose 1-day forcing is exhausted
early, and a 1-day run that stops well before its 512-day forcing ends.

In global NextGen-style realization files, ``CsvPerFeature`` forcing enables
MIAOW-managed CSV forcing for each feature. Partitioning resolves that global
forcing entry to per-task ``CsvFile`` forcing.

Diagnostics and artifacts
-------------------------

``Runtime::submit_job(...)`` returns a non-blocking ``JobHandle``; ``get()``
yields a ``JobRecord`` with per-task records and job runtime. An output write
failure for ``job_record.json`` is reported by ``JobHandle::get()`` as an
exception and does not change task statuses. If ``RuntimeOptions::output_root_``
or CLI ``--output_root`` is set, MIAOW writes:

.. code-block:: text

   <output_root>/
   `-- run_<run_id>/
       `-- job_<job_id>/
           |-- job_record.json
           |-- <feature_id or task_<task_id>>.json
           `-- results/
               `-- <feature_id or task_<task_id>>.csv

Persisted run directories start at ``run_0``. When an output root already
contains run directories, the next run uses the largest existing run ID plus
one so as not to overwrite any existing output.

Each task receives ``Task::task_output_dir_`` for the job ``results/``
directory and ``Task::task_output_name_`` for task-specific artifact filenames.
BMI tasks write the resolved BMI pipeline config in the job directory and one
CSV file per task under ``results/``. When a module declares
``output_variables``, those BMI variables are written as CSV columns in
declaration order; otherwise all ``GetOutputVarNames()`` variables are written.

Tasks report job-relative filenames such as ``cat-552.json`` and
``results/cat-552.csv`` in ``TaskResult::artifact_paths_``. ``job_record.json``
records those paths under each task record's ``result`` field and records each
task's descriptive ``Task::metadata_``, but it does not embed artifact file
contents. If a task has non-empty ``metadata_["feature_id"]``, that value
becomes the output file stem; otherwise the runtime uses ``task_<task_id>``.
Output stems must be unique within one job and cannot contain path separators.

Subprocess BMI tasks use task-specific ``bmi_subprocess_*_input.json``,
``bmi_subprocess_*_result.json``, ``bmi_subprocess_*_stdout.log``, and
``bmi_subprocess_*_stderr.log`` files as parent-child control files. These
files are removed after the parent reads a succeeded child result; if setup,
launch, result parsing, or task execution fails, remaining control files are
left in place for diagnostics. When no task output directory is assigned, these
subprocess control files use a temporary directory instead.
