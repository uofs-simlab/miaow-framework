Examples
========

Run built examples from their own ``examples/<example_name>`` directories:

.. code-block:: sh

   cd examples/00_hello
   ./miaow_hello
   cd ../01_heat
   ./miaow_heat
   cd ../02_soil_freeze_thaw
   ./miaow_soil_freeze_thaw --output_root ./output --max_workers 2
   cd ../03_dynamic_bmi
   ./miaow_dynamic_bmi --output_root ./output --max_workers 2

Included examples
-----------------

- ``examples/00_hello``: minimal orchestration and diagnostics output
- ``examples/01_heat``: BMI integration using the `CSDMS heat example <https://github.com/csdms/bmi-example-cxx>`_
- ``examples/02_soil_freeze_thaw``: many-task `SoilFreezeThaw <https://github.com/NOAA-OWP/SoilFreezeThaw>`_ workflow with per-task forcing, including matched, short, and long forcing cases
- ``examples/03_dynamic_bmi``: dynamic C++ and Fortran BMI plugin integration using the CSDMS heat examples
- ``examples/04_summa``: optional `SUMMA <https://github.com/CH-Earth/summa>`_ run using subprocess pipeline execution and one MIAOW task per staged catchment feature described by a NextGen-style realization
- ``examples/05_libtrixi_bmi``: optional `Trixi.jl <https://github.com/trixi-framework/Trixi.jl>`_ workflow using the ``libtrixi`` dynamic BMI plugin

SUMMA example
-------------

The ``examples/04_summa`` example builds a SUMMA BMI library from a SUMMA source tree and runs the packaged gauge realization by default. It is not built by default. SUMMA requires `NetCDF <https://www.unidata.ucar.edu/software/netcdf/>`_ C/Fortran and BLAS/LAPACK support. Enable it by adding ``04_summa`` to ``MIAOW_EXAMPLES``:

.. code-block:: sh

   cmake -S . -B build \
     -DMIAOW_EXAMPLES=04_summa
   cmake --build build --target miaow_summa

Then run it from the example directory:

.. code-block:: sh

   cd examples/04_summa
   ./miaow_summa --output_root ./output

These flags control the SUMMA build:

.. list-table::
   :header-rows: 1
   :widths: 35 30 45

   * - Flag
     - Default
     - Description
   * - ``-DMIAOW_SUMMA_MODEL_ROOT``
     - ``${MIAOW_DEFAULT_MODEL_BASE_DIR}/summa``
     - Local or fetched SUMMA source tree
   * - ``-DMIAOW_SUMMA_LIBRARY_LINKS``
     - ``-framework Accelerate`` on macOS, empty otherwise
     - LAPACK/BLAS link flags passed to SUMMA

Trixi.jl example
----------------

The ``examples/05_libtrixi_bmi`` example runs `Trixi.jl <https://github.com/trixi-framework/Trixi.jl>`_ configs through MIAOW's ``libtrixi`` dynamic BMI plugin path. It is not built by default. Enable it by adding ``05_libtrixi_bmi`` to ``MIAOW_EXAMPLES``:

.. code-block:: sh

   cmake -S . -B build \
     -DMIAOW_EXAMPLES=05_libtrixi_bmi
   cmake --build build --target miaow_libtrixi_bmi

These flags control the ``libtrixi`` paths:

.. list-table::
   :header-rows: 1
   :widths: 35 30 45

   * - Flag
     - Default
     - Description
   * - ``-DMIAOW_LIBTRIXI_BMI_ROOT``
     - ``${MIAOW_DEFAULT_MODEL_BASE_DIR}/libtrixi-bmi``
     - Local or fetched ``libtrixi-bmi`` checkout
   * - ``-DLIBTRIXI_INSTALL_DIR``
     - ``<libtrixi-bmi>/build/libtrixi-install``
     - ``libtrixi`` installation root
   * - ``-DLIBTRIXI_PROJECT_DIR``
     - ``<libtrixi-bmi>/build/libtrixi-julia``
     - Julia project directory
   * - ``-DLIBTRIXI_DEPOT_PATH``
     - ``<libtrixi-bmi>/build/julia-depot``
     - Optional Julia depot passed to ``trixi_initialize``

Configuring model examples independently
----------------------------------------

These flags are relevant when configuring from an example directory or another project:

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
     - Default base directory where external model sources are resolved or fetched
   * - ``-DEXECUTABLE_TARGET``
     - example default
     - Override executable target name

The helpers ``cmake/miaow_bmi_model.cmake`` and ``cmake/miaow_setup_example.cmake`` provide ``miaow_add_bmi_model(...)`` and ``miaow_add_workflow_test(...)``, respectively, for model integration and workflow tests. Model examples keep their MIAOW-specific source and input files under ``examples/<example_name>/``.

External BMI model source is resolved separately: each model-backed example uses its corresponding ``MIAOW_*_MODEL_ROOT`` override when set, otherwise it resolves to ``${MIAOW_DEFAULT_MODEL_BASE_DIR}/<model-subdir>``. If a required source directory is missing and the example declares a Git repository and tag, CMake fetches the model from GitHub into the resolved location under ``MIAOW_DEFAULT_MODEL_BASE_DIR``.

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
     - Timeout for one task execution, in seconds; a timed-out subprocess task terminates its child process
   * - ``--job_request_timeout``
     - ``300``
     - Timeout for job result requests, in seconds
   * - ``--output_root``
     - ``""``
     - Root directory path for runtime-managed output folders, disabled by default

Public CLI parsers throw ``std::runtime_error`` for unrecognized arguments, missing option values, and invalid option values. Example executables catch these errors, print the message, and exit with status ``1``.
