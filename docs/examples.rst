Examples
========

Examples are opt-in workflows that demonstrate MIAOW features and model integrations. Select and build them through the repository build as described in :ref:`building-examples`, then run them from their own ``examples/<example_name>`` directories:

.. code-block:: sh

   cd examples/00_hello
   ./miaow_hello
   cd ../01_heat
   ./miaow_heat
   cd ../02_soil_freeze_thaw
   ./miaow_soil_freeze_thaw --output_root ./output --max_workers 2
   cd ../03_dynamic_bmi
   ./miaow_dynamic_bmi --output_root ./output --max_workers 2
   cd ../07_job_controller
   ./miaow_job_controller

Default examples
----------------

- `examples/00_hello <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/00_hello>`_: minimal orchestration and diagnostics output
- `examples/01_heat <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/01_heat>`_: BMI integration using the `CSDMS heat example <https://github.com/csdms/bmi-example-cxx>`_
- `examples/02_soil_freeze_thaw <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/02_soil_freeze_thaw>`_: static `SoilFreezeThaw <https://github.com/NOAA-OWP/SoilFreezeThaw>`_ BMI integration with aligned per-task forcing
- `examples/03_dynamic_bmi <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/03_dynamic_bmi>`_: in-process dynamic C++ and Fortran BMI plugin integration using the CSDMS heat examples
- `examples/07_job_controller <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/07_job_controller>`_: one initial hello task whose completion generates two additional tasks

Additional examples
-------------------

- `examples/04_summa <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/04_summa>`_: `SUMMA <https://github.com/CH-Earth/summa>`_ run using subprocess pipeline execution and one MIAOW task per staged catchment feature described by a NextGen-style realization
- `examples/05_libtrixi_bmi <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/05_libtrixi_bmi>`_: `Trixi.jl <https://github.com/trixi-framework/Trixi.jl>`_ workflow using the ``libtrixi`` dynamic BMI plugin in a single-task subprocess

SUMMA
^^^^^

The ``examples/04_summa`` example builds a SUMMA BMI library from a SUMMA source tree and runs the packaged gauge realization by default. It is not part of the default example set. SUMMA requires `NetCDF <https://www.unidata.ucar.edu/software/netcdf/>`_ C/Fortran and BLAS/LAPACK support. Select it with ``MIAOW_EXAMPLES=04_summa``, then run it from the example directory:

.. code-block:: sh

   cd examples/04_summa
   ./miaow_summa --output_root ./output

The following example-specific flags control the SUMMA build:

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

Trixi.jl
^^^^^^^^

The ``examples/05_libtrixi_bmi`` example runs `Trixi.jl <https://github.com/trixi-framework/Trixi.jl>`_ configs through MIAOW's ``libtrixi`` dynamic BMI plugin path. It is not part of the default example set, so it should be selected using ``MIAOW_EXAMPLES=05_libtrixi_bmi``. The following example-specific flags control the ``libtrixi`` paths:

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
     - Timeout for one task execution, in seconds; MIAOW terminates the child process when a subprocess task exceeds this timeout
   * - ``--job_request_timeout``
     - ``300``
     - Maximum runtime for one job, in seconds
   * - ``--output_root``
     - ``""``
     - Root directory path for runtime-managed output folders, disabled by default

Public CLI parsers throw ``std::runtime_error`` for unrecognized arguments, missing option values, and invalid option values. Example executables catch these errors, print the message, and exit with status ``1``.
