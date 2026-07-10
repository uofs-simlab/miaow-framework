BMI integration
===============

MIAOW runs statically typed C++ BMI models and dynamically loaded C++ or
Fortran BMI plugins through the same task interface. Use this page to configure
tasks, select dynamic-task execution, and understand workflow behaviour. See
:doc:`api` for constructor, member, and function details.

BMI task lifecycle
------------------

Build individual tasks with ``make_bmi_task(...)`` and task sets with
``make_bmi_tasks(...)``. Both functions translate typed BMI configuration into
the generic ``Task`` representation used by the runtime.

During execution, MIAOW writes the resolved BMI configuration beside the task
results as ``<task_output_name>.json`` and runs the BMI lifecycle:

1. ``Initialize(<task_output_name>.json)``
2. zero or more ``Update()`` calls
3. ``Finalize()``

Static and dynamic tasks use the same forcing, output, and artifact handling.

Minimal setup patterns
----------------------

Choose a setup pattern based on where the BMI model implementation comes from:
a statically typed C++ model, a dynamic plugin, or a realization file.

Static BMI model
~~~~~~~~~~~~~~~~

Use this path when the concrete BMI model type is known when compiling the
executable. ``StaticBmiSimulationUnit<YourModel>`` constructs one fresh model
object per task.

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

Use this path when the model is loaded from a C++ or Fortran plugin library.
``DynamicBmiConfig`` contains the plugin, model, forcing, output, and execution
settings for one task.

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId dynamic_bmi =
       registry.add<miaow::DynamicBmiSimulationUnit>();

   const miaow::DynamicBmiConfig config{
       .backend_ = miaow::BmiPluginBackend::fortran,
       .plugin_path_ = plugin_path,
       .init_config_ = init_config,
       .execution_mode_ = miaow::ExecutionMode::persistent_subprocess,
   };
   const miaow::Task task = miaow::make_bmi_task(dynamic_bmi, config);

Dynamic C++ plugins must export C-linkage ``bmi_model_create()`` and
``bmi_model_destroy(...)`` symbols. Dynamic Fortran plugins must export
``register_bmi(void**)`` for the linked
`NOAA-OWP iso_c_fortran_bmi bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_.
See ``examples/03_dynamic_bmi`` for concrete C++ and Fortran plugin exports.

NextGen-style realization-driven task creation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use this path when a
`NextGen <https://noaa-owp.github.io/ngen/>`_-style realization should produce
one dynamic BMI task per feature (e.g., per catchment).

.. code-block:: cpp

   const miaow::BmiRealization realization(realization_path);

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId dynamic_bmi =
       registry.add<miaow::DynamicBmiSimulationUnit>();

   const std::vector<miaow::Task> tasks = miaow::make_bmi_tasks(
       dynamic_bmi, realization,
       miaow::ExecutionMode::persistent_subprocess);

``BmiRealization`` resolves the plugin, initialization, forcing, feature, and
output-root values. ``make_bmi_tasks(...)`` builds the dynamic task set without
requiring post-construction task mutation.

Choose where dynamic tasks run
------------------------------

Set ``DynamicBmiConfig::execution_mode_`` for direct dynamic tasks or pass an
execution mode to the realization overload of ``make_bmi_tasks(...)``. Static
BMI units always run in the parent process.

.. list-table::
   :header-rows: 1
   :widths: 30 35 35

   * - Execution mode
     - Use when
     - Behaviour to know
   * - ``ExecutionMode::in_process``
     - The plugin can run safely in the parent process
     - This is the default task execution mode.
   * - ``ExecutionMode::single_task_subprocess``
     - Each task needs a fresh process
     - The subprocess stops after one task result.
   * - ``ExecutionMode::persistent_subprocess``
     - Tasks benefit from process reuse while keeping parent-process isolation
     - One process is reused per job worker; each task still gets a fresh BMI
       lifecycle.

Subprocess modes are available for dynamic BMI tasks.
``DynamicBmiConfig::subprocess_path_`` selects a non-default subprocess
executable. See :ref:`subprocess-execution` for process ownership, reuse,
lifetime, and concurrency behaviour.

Reading forcing data
--------------------

Set ``forcing_file_`` in ``StaticBmiConfig`` or ``DynamicBmiConfig`` to enable
the internal BMI forcing module. The forcing module is always appended after
the model; when no forcing is configured, it is initialized with an empty
config and acts as a no-op. With configured forcing,
the module reads a ``CsvFile`` entry and exposes data columns as BMI outputs.
MIAOW derives explicit scalar couplings from ``variables_names_map_``, applies
them immediately before model updates, and then advances the forcing source.

The variable map is interpreted as ``BMI input variable -> CSV header``. If it
is empty, MIAOW derives an identity map from ``GetInputVarNames()`` and expects
same-named CSV headers. ``output_variables_`` controls which BMI output names
are written to task CSV output; when empty, CSV output uses all names returned
by ``GetOutputVarNames()``.

MIAOW's external forcing reader currently only supports CSV files. The file
must define a ``time`` or ``Time`` column plus all required numeric data
columns. Supported timestamp formats are ``YYYY-MM-DD HH:MM:SS`` and
``YYYY/MM/DD HH:MM:SS``. MIAOW treats the first forcing row as the model-start
forcing values. Later forcing rows are streamed as the model advances; each
row-to-row timestamp delta must match the receiving BMI model timestep after
unit normalization.

Exhausting the forcing data before the model's ``end_time`` fails the task
with a ``Forcing alignment error``; forcing rows beyond ``end_time`` are
silently ignored. The ``examples/02_soil_freeze_thaw`` workflow includes
matched and mismatched model end time and forcing series lengths.

In global `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization
files, ``CsvPerFeature`` forcing enables MIAOW-managed CSV forcing for each
feature. Realization parsing resolves that entry to per-task ``CsvFile``
forcing.

Diagnostics and artifacts
-------------------------

``Runtime::submit_job(...)`` returns a non-blocking ``JobHandle``; ``get()``
yields a ``JobRecord`` with per-task records and job runtime. An output write
failure for ``job_record.json`` is reported by ``JobHandle::get()`` as an
exception and does not change task statuses.

When ``RuntimeOptions::output_root_`` or CLI ``--output_root`` is set, MIAOW
writes:

.. code-block:: text

   <output_root>/
   `-- run_<run_id>/
       `-- job_<job_id>/
           |-- job_record.json
           |-- <feature_id or task_<task_id>>.json
           `-- results/
               `-- <feature_id or task_<task_id>>.csv

Persisted run directories start at ``run_0`` and advance to the largest
existing run ID plus one. BMI tasks write the resolved BMI config in the
job directory and one CSV file per task under ``results/``. Task artifact paths
are job-relative, for example ``cat-552.json`` and ``results/cat-552.csv``;
``job_record.json`` records those paths and each task's ``Task::metadata_`` but
does not embed artifact contents. A non-empty ``metadata_["feature_id"]``
becomes the output file stem; otherwise MIAOW uses ``task_<task_id>``. Output
stems must be unique within one job and cannot contain path separators.

Subprocess tasks also write ``subprocess_*_stdout.log`` and
``subprocess_*_stderr.log`` diagnostics. Logs are removed after successful task
or persistent-worker shutdown, and retained when setup, launch, actor exchange,
shutdown, or task execution fails. Parent-side process failures report
``Subprocess error``. When no task output directory is assigned, subprocess logs
use a temporary directory.
