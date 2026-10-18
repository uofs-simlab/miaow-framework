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
- `examples/07_job_controller <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/07_job_controller>`_: computes a square root using Heron's method, where tasks output the next estimate and residual for the job controller to check for convergence and generate the next task

Additional examples
-------------------

- `examples/04_summa <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/04_summa>`_: `SUMMA <https://github.com/CH-Earth/summa>`_ run using subprocess pipeline execution and one MIAOW task per staged catchment feature described by a NextGen-style realization
- `examples/05_libtrixi_bmi <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/05_libtrixi_bmi>`_: `Trixi.jl <https://github.com/trixi-framework/Trixi.jl>`_ workflow using the ``libtrixi`` dynamic BMI plugin in a single-task subprocess
- `examples/06_fuse_bmi <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/06_fuse_bmi>`_: deterministic 256-member Saltelli ensemble using the `FUSE <https://github.com/uofs-simlab/fuse>`_ dynamic Fortran BMI plugin
- `examples/08_particle_swarm <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/08_particle_swarm>`_: compares batch-synchronous and asynchronous particle swarm optimization on Rastrigin, with randomized evaluation delays and Matplotlib playback

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

SUMMA is distributed under the `GNU General Public License version 3 <https://github.com/CH-Earth/summa/blob/master/COPYING>`_.

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

`Trixi.jl <https://github.com/trixi-framework/Trixi.jl/blob/main/LICENSE.md>`_ and `libtrixi <https://github.com/trixi-framework/libtrixi/blob/main/LICENSE.md>`_ are distributed under the MIT License.

FUSE
^^^^

The ``examples/06_fuse_bmi`` example runs a deterministic 256-member Saltelli ensemble through FUSE's dynamic Fortran BMI plugin. It requires NetCDF Fortran and Python 3 with no additional packages. The NetCDF parameter files are tracked in ``examples/06_fuse_bmi/data/paramsets``, while CMake locates or downloads the forcing, elevation-band, and settings data. This case is not part of the default example set.

Using six workers, the ensemble can be run from the example root directory with the following commands:

.. code-block:: sh

   cmake -S . -B build \
       -DBUILD_EXAMPLES=ON \
       -DMIAOW_EXAMPLES=06_fuse_bmi \
       -DMIAOW_FUSE_FETCH_DATA=ON
   cmake --build build --target miaow_fuse_bmi
   cd examples/06_fuse_bmi
   ./miaow_fuse_bmi --max_workers 6 --task_request_timeout 900 --job_request_timeout 14400

The following example-specific flags control the FUSE model and data paths:

.. list-table::
   :header-rows: 1
   :widths: 35 30 45

   * - Flag
     - Default
     - Description
   * - ``-DMIAOW_FUSE_MODEL_ROOT``
     - ``${MIAOW_DEFAULT_MODEL_BASE_DIR}/fuse``
     - Local FUSE checkout; CMake fetches the pinned revision when it is absent
   * - ``-DMIAOW_FUSE_DATA_ROOT``
     - Empty
     - Extracted ``fuse_grid`` dataset; CMake first checks ``${MIAOW_DEFAULT_MODEL_BASE_DIR}/fuse_grid`` when this is empty
   * - ``-DMIAOW_FUSE_FETCH_DATA``
     - ``OFF``
     - Download the verified FUSE grid archive when no local dataset is available

The opt-in download uses the `FUSE grid test case <https://dl.dropboxusercontent.com/s/g5193e0n01ao33d/fuse_grid.zip?dl=0>`_. CMake stores the extracted dataset and generated task configurations under ``build/fuse_grid_data``, which is ignored by Git. The provided parameter sets were generated using FUSE's `bmi/generate_param_sets.py <https://github.com/uofs-simlab/fuse/blob/d73d24bb8678908973ca6f82f9d6eb6405cbb85f/bmi/generate_param_sets.py>`_ script at revision ``d73d24bb8678908973ca6f82f9d6eb6405cbb85f`` with ``N_BASE_SAMPLES = 8`` and ``CONSTRAINTS_FILE`` set to the ``settings/fuse_zConstraints_snow.txt`` file from the downloaded ``fuse_grid`` dataset. The script creates 256 parameter sets in NetCDF format, which are tracked in this repository under ``examples/06_fuse_bmi/data/paramsets``.

The FUSE repository is distributed under the `GNU General Public License version 3 <https://github.com/uofs-simlab/fuse/blob/master/COPYING>`_.

Particle swarm optimization
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``08_particle_swarm`` example compares two methods for scheduling particle swarm optimization. Each task evaluates one particle's position on the Rastrigin function, with randomized delays to simulate varying evaluation costs.

- ``batch_synchronous``: the controller waits for every particle in a generation to finish, then updates the whole swarm using that generation's best result and submits the next generation.
- ``asynchronous``: the controller updates each particle as its result arrives, using the best result available at that time, and submits its next evaluation without waiting for the other particles.

The synchronous job runs to completion first, then the asynchronous job starts. Both start from identical particle positions and velocities, using 32 particles and eight workers by default. Each stops submitting work after 30 seconds or 100,000 evaluations; queued and active evaluations finish normally.

Select, build, and run this additional example from the repository root:

.. code-block:: sh

   cmake -S . -B build -DBUILD_EXAMPLES=ON -DMIAOW_EXAMPLES=08_particle_swarm
   cmake --build build --target miaow_particle_swarm
   cd examples/08_particle_swarm
   ./miaow_particle_swarm

Results are saved under ``output/run_<id>``. To compare particle histories, worker usage, and convergence, use the provided Python script:

.. code-block:: sh

   python3 -m venv .venv
   .venv/bin/python -m pip install -r requirements.txt
   .venv/bin/python animate.py "output/run_<id>"

Replace ``<id>`` with the run number in the path printed by the executable (``0`` for the first run). Figures are written to its ``figures`` subdirectory; MP4 export requires FFmpeg.

Example command-line options
----------------------------

Examples accept the following runtime command-line options where applicable. The table shows ``RuntimeOptions`` defaults; individual examples may override them. For example, ``08_particle_swarm`` defaults to eight workers and an output root of ``output``.

.. list-table::
   :header-rows: 1
   :widths: 30 30 50

   * - Option
     - Runtime default
     - Description
   * - ``--max_workers``
     - ``1``
     - Maximum number of worker actors used for each job
   * - ``--task_request_timeout``
     - ``5``
     - Task-response timeout, in seconds; timed-out tasks fail. Only subprocess tasks are terminated; running in-process evaluations are not interrupted.
   * - ``--job_request_timeout``
     - ``300``
     - Job timeout, in seconds; unfinished tasks fail and worker shutdown is requested. Only subprocess tasks are terminated; running in-process evaluations are not interrupted.
   * - ``--output_root``
     - ``""``
     - Root directory path for runtime-managed output folders, disabled by default

Public CLI parsers throw ``std::runtime_error`` for unrecognized arguments, missing option values, and invalid option values. Example executables catch these errors, print the message, and exit with status ``1``.
