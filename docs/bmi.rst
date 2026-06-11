BMI integration
=======================================

Use ``BmiSimulationUnit<BmiModelType>`` from ``include/miaow_bmi.hpp`` to run
BMI-compatible models. The simulation unit creates one task-level model or
pipeline, runs the BMI lifecycle, writes artifacts, and returns a
``TaskResult``.

There are two common registration patterns:

- ``BmiSimulationUnit<YourModel>`` runs one linked BMI model per task, where
  ``YourModel`` is a subtype of ``miaow::bmi_api::Bmi`` linked into the
  executable and supplied directly to the task pipeline.
- ``BmiSimulationUnit<BmiPipeline>`` runs a complete pipeline described by the
  task config. The config may contain dynamic plugin modules, multiple modules,
  or modules obtained from partitioning a
  `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization file.

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
initialization file plus optional forcing and variable mapping. Linked model
units execute in the parent process because the subprocess worker only receives
serialized ``BmiPipelineConfig`` task data.

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId heat =
       registry.add<miaow::BmiSimulationUnit<BmiHeat>>();

   const miaow::BmiPipelineConfig pipeline_config =
       miaow::make_bmi_model_config(heat_config);
   const std::vector<miaow::Task> tasks{
       {heat, miaow::bmi_pipeline_config_to_value(pipeline_config)}};

At execution time, MIAOW wraps the model in ``BmiPipeline`` so the same
lifecycle, forcing, output writing, and artifact tracking are used for BMI
work.

Executing a dynamic BMI model
-----------------------------

Use ``BmiSimulationUnit<BmiPipeline>`` when the task should run one or more BMI
models loaded from plugin libraries. ``BmiModuleConfig`` describes each plugin
module, including the initialization config path used for that task.

.. code-block:: cpp

   const miaow::BmiModuleConfig module_config(
       "bmi_fortran", miaow::DynamicBmiBackend::fortran, plugin_path,
       init_config);

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId pipeline =
       registry.add<miaow::BmiSimulationUnit<miaow::BmiPipeline>>();

   const miaow::BmiPipelineConfig pipeline_config =
       miaow::BmiPipelineConfig({module_config});
   const std::vector<miaow::Task> tasks{
       {pipeline, miaow::bmi_pipeline_config_to_value(pipeline_config)}};

At execution time, ``BmiPipeline`` constructs ``DynamicBmiModel`` modules from
the serialized config. This is the same execution path used for realization
workflows, hand-built multi-model pipelines, and subprocess tasks.

For consistency with NextGen, the C++ backend expects plugins to export
C-linkage symbols named ``bmi_model_create()`` and ``bmi_model_destroy(...)``.
The Fortran backend expects plugins to export ``register_bmi(void**)`` for the
linked
`NOAA-OWP iso_c_fortran_bmi bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_.

For C++ plugins, export the creation and destruction functions with C linkage:

.. code-block:: cpp

   extern "C" bmi::Bmi* bmi_model_create() {
       return new YourBmiModel();
   }

   extern "C" void bmi_model_destroy(bmi::Bmi* model) {
       delete model;
   }

For Fortran plugins, export ``register_bmi`` with ``bind(C)``:

.. code-block:: fortran

   function register_bmi(this) result(bmi_status) bind(C, name="register_bmi")
       use, intrinsic :: iso_c_binding, only: c_int, c_loc, c_ptr
       use bmif_2_0_iso, only: BMI_FAILURE, BMI_SUCCESS
       use your_model_module, only: your_bmi_model
       use iso_c_bmif_2_0, only: box
       implicit none

       type(c_ptr) :: this
       integer(kind=c_int) :: bmi_status
       type(your_bmi_model), pointer :: bmi_model
       type(box), pointer :: bmi_box

       allocate(bmi_model)
       allocate(bmi_box)
       bmi_box%ptr => bmi_model

       if (associated(bmi_box) .and. associated(bmi_box%ptr)) then
           this = c_loc(bmi_box)
           bmi_status = BMI_SUCCESS
       else
           bmi_status = BMI_FAILURE
       end if
   end function register_bmi

Executing a realization-driven BMI pipeline
-------------------------------------------

Use ``BmiSimulationUnit<BmiPipeline>`` when the task's ``BmiPipelineConfig``
describes the model modules directly. This is the path used by realization
workflows and by pipelines with more than one model module.

.. code-block:: cpp

   miaow::PartitionedBmiRealization realization(realization_path);

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId pipeline =
       registry.add<miaow::BmiSimulationUnit<miaow::BmiPipeline>>();

   std::vector<miaow::Task> tasks =
       miaow::build_bmi_realization_tasks(realization, pipeline);
   for (miaow::Task& task : tasks) {
       task.execution_mode_ = miaow::ExecutionMode::persistent_subprocess;
   }

``PartitionedBmiRealization`` reads a
`NextGen <https://noaa-owp.github.io/ngen/>`_-style realization and produces one
``BmiPipelineConfig`` per feature. ``build_bmi_realization_tasks(...)`` turns
those feature configs into MIAOW tasks for the registered pipeline unit.

``ExecutionMode::single_task_subprocess`` runs one BMI lifecycle in a subprocess
and stops the subprocess after the task result. This is useful for BMI models
that require a fresh process for every task. An empty ``Task::subprocess_path_``
uses the compile-time default ``miaow_subprocess`` runner path; a non-empty
value selects that subprocess executable for the task.

``ExecutionMode::persistent_subprocess`` is available for serialized
``BmiPipeline`` tasks. It uses the same subprocess worker backend as
single-task mode and reuses one subprocess process per job worker, while each
task still constructs and finalizes a fresh ``BmiPipeline`` inside that process.
Persistent subprocesses are scoped to one submitted job and are bounded by
``RuntimeOptions::max_workers_``. The SUMMA example uses persistent subprocess
mode.

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

In global `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization
files, ``CsvPerFeature`` forcing enables MIAOW-managed CSV forcing for each
feature. Partitioning resolves that global forcing entry to per-task ``CsvFile``
forcing.

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

Subprocess tasks use a dedicated subprocess-to-parent pipe for the CAF endpoint
exchange, plus ``subprocess_*_stdout.log`` and ``subprocess_*_stderr.log`` files
for diagnostics. The parent launches the subprocess, reads one endpoint JSON
line from the pipe, sends ``Task`` values to the subprocess actor over CAF, and
receives ``TaskResult`` replies. Task-scoped subprocess logs are removed after a
succeeded task result and subprocess stop. Persistent subprocess logs are
removed when the job worker stops the process after all tasks handled by that
process succeeded. If setup, launch, actor exchange, subprocess shutdown, or
task execution fails, remaining logs are left in place for diagnostics.
Parent-side process failures report ``Subprocess error``. When no task output
directory is assigned, these subprocess logs use a temporary directory instead.
