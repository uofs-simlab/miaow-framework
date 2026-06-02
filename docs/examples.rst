Examples
========

Run examples from their own ``examples/<example_name>`` directories:

.. code-block:: sh

   cd examples/00_hello
   ./miaow_hello
   cd ../01_heat
   ./miaow_heat
   cd ../02_soil_freeze_thaw
   ./miaow_soil_freeze_thaw --output_root ./output --max_workers 2
   cd ../03_dynamic_bmi
   ./miaow_dynamic_bmi --output_root ./output --max_workers 2
   cd ../04_summa
   ./miaow_summa --realization data/realization_config.json --output_root ./output

Included examples
-----------------

- ``examples/00_hello``: minimal orchestration and diagnostics output
- ``examples/01_heat``: BMI integration using the
  `CSDMS heat example <https://github.com/csdms/bmi-example-cxx>`_
- ``examples/02_soil_freeze_thaw``: many-task
  `SoilFreezeThaw <https://github.com/NOAA-OWP/SoilFreezeThaw>`_ workflow
  with per-task forcing, including matched, short, and long forcing cases
- ``examples/03_dynamic_bmi``: dynamic C++ and Fortran BMI plugin integration
  using the CSDMS heat examples
- ``examples/04_summa``:
  `SUMMA <https://github.com/CH-Earth/summa>`_ run using subprocess pipeline
  execution and one MIAOW task per staged catchment feature described by a
  NextGen-style realization

Example layout
--------------

Each example directory follows a common layout:

.. code-block:: text

   examples/<example_name>/
     data/             # example input data, tracked in the repository
       configs/        # configuration files
       forcings/       # forcing data files, when needed
       settings/       # model settings and parameter files, when needed
     extern/           # symlinks to model sources, created by CMake
     output/           # runtime and model output, generated and not tracked

Configuring model examples independently
----------------------------------------

These flags are relevant when configuring from an example directory or another
project:

.. list-table::
   :header-rows: 1
   :widths: 35 30 45

   * - Flag
     - Default
     - Description
   * - ``-DMIAOW_ROOT``
     - ``<example_dir>/../..``
     - Path to the ``miaow-framework`` source tree
   * - ``-DMIAOW_DEFAULT_MODEL_BASE_DIR``
     - ``${MIAOW_ROOT}/third_party``
     - Default base directory where external model sources are resolved or
       fetched
   * - ``-DEXECUTABLE_TARGET``
     - example default
     - Override executable target name

The helper ``cmake/miaow_bmi_model.cmake`` provides
``miaow_add_bmi_model(...)`` and ``miaow_add_workflow_test(...)`` for model
integration and workflow tests. Model examples keep their MIAOW-specific
source and input files under ``examples/<example_name>/``.

External BMI model source is resolved separately: each model-backed example
uses its corresponding ``MIAOW_*_MODEL_ROOT`` override when set, otherwise it
resolves to ``${MIAOW_DEFAULT_MODEL_BASE_DIR}/<model-subdir>``. If a required
source directory is missing and the example declares a Git repository and tag,
CMake fetches the model from GitHub into the resolved location under
``MIAOW_DEFAULT_MODEL_BASE_DIR``.

Example command-line options
----------------------------

Examples use the following runtime command-line options where applicable:

.. list-table::
   :header-rows: 1
   :widths: 30 30 50

   * - Option
     - Default
     - Description
   * - ``--max_workers``
     - ``1``
     - Maximum number of worker actors used for each job
   * - ``--task_request_timeout``
     - ``5``
     - Timeout for task requests sent to worker actors, in seconds
   * - ``--job_request_timeout``
     - ``300``
     - Timeout for job result requests, in seconds
   * - ``--output_root``
     - ``""``
     - Root directory path for runtime-managed output folders, disabled by
       default
   * - ``--realization``
     - ``data/realization_config.json``
     - BMI realization path

Public CLI parsers throw ``std::runtime_error`` for unrecognized arguments,
missing option values, and invalid option values. Example executables catch
these errors, print the message, and exit with status ``1``.
