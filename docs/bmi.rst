BMI integration
===============

This page describes the Basic Model Interface (BMI) integration paths and how to configure tasks for different BMI model execution patterns; see the :doc:`examples` section for concrete workflows and the :doc:`api` for constructor, member, and function details.

BMI task lifecycle
------------------

MIAOW supports the execution of BMI-compatible models through two different paths:

1. Static BMI model execution, where the concrete model type ``BmiModelType`` is known at compile time and the model is constructed and executed in the parent process through a ``StaticBmiSimulationUnit<BmiModelType>`` configured with a ``StaticBmiConfig``.
2. Dynamic BMI plugin execution, where the model is loaded from a C++ or Fortran plugin library and executed in the parent process as a ``DynamicBmiSimulationUnit`` or in a subprocess through the general ``SubprocessSimulationUnit`` path. These models are configured either with a ``DynamicBmiConfig`` or through a NextGen-style realization file that produces one dynamic BMI task per feature, which represents a spatial subdivision of the model domain, for example, a hydrologic catchment.

After a single BMI task is constructed using ``make_bmi_task(...)`` or a task vector is constructed using ``make_bmi_tasks(...)``, MIAOW executes the tasks through the selected simulation unit implementation, using a ``BmiPipeline`` internally to manage the BMI lifecycle for each task. After loading the pipeline configuration, ``BmiPipeline`` runs the BMI lifecycle for its components, which currently include a single BMI model and an optional internal forcing module. The pipeline lifecycle, which is implemented in `src/bmi/execute.cpp <https://github.com/uofs-simlab/miaow-framework/blob/main/src/bmi/execute.cpp>`_, consists of three phases:

1. Call ``Initialize()`` on every component (including the forcing module if configured) with the component's ``init_config_``.
2. Perform zero or more complete pipeline ``Update()`` calls, which apply the current forcing values (see :ref:`bmi-forcing-data`), advance the model, and advance the forcing source. If MIAOW-managed output is enabled, the enclosing lifecycle writes output at the configured interval (see :ref:`bmi-output`).
3. ``Finalize()`` every initialized component.

Task configuration options
--------------------------

The setup pattern depends on whether the BMI model implementation comes from a statically typed C++ model, a dynamic plugin, or a realization file.

Selecting task execution
~~~~~~~~~~~~~~~~~~~~~~~~

The ``SimulationUnitId`` passed to ``make_bmi_task(...)`` or ``make_bmi_tasks(...)`` selects execution. An ID returned by ``SimulationUnitRegistry::add<StaticBmiSimulationUnit<BmiModelType>>()`` selects static execution in the parent process, while an ID returned by ``SimulationUnitRegistry::add<DynamicBmiSimulationUnit>()`` selects dynamic plugin execution in the parent process. An ID returned by ``SimulationUnitRegistry::add_subprocess(bmi_subprocess_executable_path(), lifetime)`` selects dynamic plugin execution in a child subprocess, which is useful for non-thread-safe models. We describe the different options below.

Static BMI model
~~~~~~~~~~~~~~~~

The static model setup applies when the concrete BMI model type is known when compiling the executable. ``StaticBmiSimulationUnit<YourModel>`` constructs one new model object per task. The following example registers the model type and constructs one task to be executed in the parent process:

.. code-block:: cpp

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId heat =
       registry.add<miaow::StaticBmiSimulationUnit<BmiHeat>>();

   const miaow::StaticBmiConfig config{
       .init_config_ = heat_config,
   };
   const miaow::Task task = miaow::make_bmi_task(heat, config);

The same registered simulation unit ID can be applied to several configurations. The following example constructs one ``Task`` for each ``StaticBmiConfig`` in a vector, using ``first_heat_config`` and ``second_heat_config`` as their initialization configurations. The resulting tasks share the registered simulation unit ID ``heat`` and execute in the parent process:

.. code-block:: cpp

   const std::vector<miaow::StaticBmiConfig> configs{
       {.init_config_ = first_heat_config},
       {.init_config_ = second_heat_config},
   };
   const std::vector<miaow::Task> tasks =
       miaow::make_bmi_tasks(heat, configs);

Dynamic BMI plugin
~~~~~~~~~~~~~~~~~~

The dynamic plugin setup applies when the model is loaded from a C++ or Fortran plugin library. ``DynamicBmiConfig`` contains the plugin, model, forcing, and output settings for one task. The following example registers parent-process execution and configures one dynamic Fortran plugin task:

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

Dynamic BMI plugins can be executed in child subprocesses by registering a ``SimulationUnitId`` through the generic ``add_subprocess(executable, lifetime)`` method rather than through ``add<DynamicBmiSimulationUnit>()``. The following example registers a single-task subprocess and constructs one dynamic BMI task to be executed in the child:

.. code-block:: cpp

   const miaow::SimulationUnitId dynamic_bmi_subprocess =
       registry.add_subprocess(
           miaow::bmi_subprocess_executable_path(),
           miaow::SubprocessLifetime::single_task);
   const miaow::Task subprocess_task =
       miaow::make_bmi_task(dynamic_bmi_subprocess, config);

In the above example, ``bmi_subprocess_executable_path()`` resolves the bundled worker that constructs and executes ``BmiPipeline`` in the subprocess. As described in :ref:`simulation-unit-execution`, ``SubprocessLifetime::single_task`` starts one child for each task, whereas ``SubprocessLifetime::persistent`` retains the child after a successful task. The same job worker reuses the child only when its next task selects a persistent registration with the same executable and the child actor remains available. Another executable can be registered when it implements the public ``Task`` to ``TaskResult`` contract described in :ref:`custom-subprocess-executables`.

Dynamic C++ plugins must export the ``bmi_model_create()`` and ``bmi_model_destroy(...)`` symbols as C linkage. Dynamic Fortran plugins must export ``register_bmi(void**)`` for the linked `NOAA-OWP iso_c_fortran_bmi bridge <https://github.com/NOAA-OWP/iso_c_fortran_bmi>`_. See ``examples/03_dynamic_bmi`` for examples of concrete C++ and Fortran plugin workflows.

.. _realization-driven-task-creation:

NextGen-style realization
~~~~~~~~~~~~~~~~~~~~~~~~~

A `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization file can be used to produce one dynamic BMI task per feature (e.g., per catchment). The following example registers the bundled worker executable, specifies a persistent subprocess lifetime, and constructs the complete task vector:

.. code-block:: cpp

   const miaow::PartitionedBmiRealization realization(realization_path);

   miaow::SimulationUnitRegistry registry;
   const miaow::SimulationUnitId bmi_subprocess =
       registry.add_subprocess(
           miaow::bmi_subprocess_executable_path(),
           miaow::SubprocessLifetime::persistent);

   const std::vector<miaow::Task> tasks =
       miaow::make_bmi_tasks(bmi_subprocess, realization);

``PartitionedBmiRealization`` resolves relative plugin, initialization, and forcing paths against the directory containing the realization file and reads feature IDs from the sibling ``catchments.geojson``. The ``make_bmi_tasks(...)`` overload used above builds the dynamic task vector all at once. Only a subset of the NextGen realization configuration is currently supported by MIAOW, with the following restrictions:

- ``global.formulations`` contains exactly one ``bmi_multi`` entry.
- The ``bmi_multi`` formulation contains exactly one ``bmi_fortran`` module.
- Global forcing uses the ``CsvPerFeature`` provider with ``path`` and ``file_pattern`` values.
- A sibling ``catchments.geojson`` provides feature IDs, and both module ``init_config`` and forcing ``file_pattern`` contain ``{{id}}``.
- The module defines ``library_file`` and ``init_config`` and sets ``uses_forcing_file`` to ``false``. An optional ``variables_names_map`` maps BMI input variables to forcing variables.
- An optional ``output_variables`` array belongs to the containing ``bmi_multi`` parameters. An omitted or empty array selects all model outputs, and an optional ``time.output_interval`` configures the output cadence.

MIAOW currently assumes that realization-driven models follow the `NextGen BMI time convention <https://noaa-owp.github.io/ngen/md_doc_2_b_m_iconventions.html>`_ and report model time in seconds.

.. _bmi-forcing-data:

Reading forcing data
--------------------

BMI models often require time-varying forcing data to be passed to the model at each update. MIAOW supports the following two forcing patterns:

1. The model reads its own forcing data from a file or other source as part of its own ``Update()`` implementation.
2. MIAOW reads the forcing data and applies it to the model at each update.

The ``forcing_`` field in ``StaticBmiConfig`` or ``DynamicBmiConfig`` selects the pattern. When the field is absent, MIAOW adds no forcing module and delegates forcing to the model. A configured ``BmiForcingConfig`` appends an internal forcing module to ``BmiPipeline`` and maps BMI input variables to forcing source columns through ``variables_names_map_``. The variable map is interpreted as ``BMI input variable -> CSV header``. An empty map derives an identity mapping from ``GetInputVarNames()`` and expects CSV headers with the same names as the corresponding model inputs. The following example configuration reads CSV forcing data and maps the ``TMP_ground_surface`` column to the BMI input variable ``ground_temperature``:

.. code-block:: cpp

   const miaow::StaticBmiConfig config{
       .init_config_ = model_config,
       .forcing_ = miaow::BmiForcingConfig{
           .format_ = miaow::BmiDataFormat::csv,
           .file_ = "forcing.csv",
           .variables_names_map_ = {
               {"ground_temperature", "TMP_ground_surface"},
           },
       },
   };

The forcing file must define a ``time`` or ``Time`` column plus all required numeric data columns. Supported timestamp formats are ``YYYY-MM-DD HH:MM:SS`` and ``YYYY/MM/DD HH:MM:SS``. MIAOW treats the first forcing row as the model-start forcing values without comparing its timestamp with the model's ``start_time``. Later forcing rows are streamed as the model advances; each row-to-row timestamp delta must match the receiving BMI model timestep after unit normalization. Because the only supported forcing source is CSV, the forcing provider for global `NextGen <https://noaa-owp.github.io/ngen/>`_-style realization files should be set to ``CsvPerFeature``. Exhausting the forcing data before the model's ``end_time`` fails the task with a ``Forcing alignment error``; forcing rows beyond ``end_time`` are silently ignored and do not cause a task failure.

.. _bmi-output:

Writing BMI output
------------------

Similarly to the forcing configuration, MIAOW can either manage the output or delegate it to the model. The ``output_`` field in ``StaticBmiConfig`` or ``DynamicBmiConfig`` selects the pattern. When the field is absent, MIAOW writes no runtime-managed output and delegates output to the model. When set to a valid ``BmiOutputConfig`` with an empty ``BmiOutputConfig::variables_`` field, MIAOW writes every variable returned by ``GetOutputVarNames()``; a non-empty ``BmiOutputConfig::variables_`` field writes the selected variables in their configured order. The CSV writer supports BMI variables whose type is ``int``, ``float``, or ``double`` and fails the task when a selected variable has another type.

The ``BmiOutputConfig::interval_`` field controls the output cadence in the units returned by the selected output model's ``GetTimeUnits()``. An absent interval writes the initialized state and every completed pipeline update. A present interval must be finite and positive; it writes the initialized state, each reachable model-time boundary, and the final state. No interpolation is performed, so the interval should be a multiple of the model timestep; this is enforced by the ``time_step_tolerance_`` field, which defaults to ``1e-4`` in the model's time units and must be finite, non-negative, and less than half of ``GetTimeStep()``. For each interval between outputs, ``BmiPipeline::UpdateUntil()`` calls ``Update()`` repeatedly until the output model time is within tolerance of the requested boundary. If the update overshoots the requested boundary by more than the configured tolerance, the task fails. As an example, the following configuration writes a single CSV file with the ``streamflow`` output variable every 24 model-time units:

.. code-block:: cpp

   const miaow::DynamicBmiConfig config{
       .backend_ = miaow::DynamicBmiBackend::fortran,
       .plugin_path_ = plugin_path,
       .output_ = miaow::BmiOutputConfig{
           .variables_ = {"streamflow"},
           .interval_ = 24.0,
       },
   };

Writing a BMI output CSV requires a non-empty ``RuntimeOptions::output_root_`` or CLI ``--output_root`` value. When the output root is empty, MIAOW does not write BMI output CSVs, resolved pipeline configuration JSON, or job metadata. Subprocess execution still writes temporary stdout and stderr logs, which are removed after successful cleanup and retained after failures. A configured output root still stores non-CSV artifacts when BMI output is disabled.

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

Run directory IDs start at ``run_0``; each new run uses one greater than the largest existing ID. MIAOW writes the resolved BMI config in the job directory and, when BMI output is enabled, one CSV file per task under ``results/``. Failed subprocess execution can also retain ``subprocess_*_stdout.log`` and ``subprocess_*_stderr.log`` in the job directory. A non-empty ``metadata_["feature_id"]`` becomes the output file stem; otherwise MIAOW uses ``task_<task_id>``. Output stems must be unique within one job and cannot contain path separators.
