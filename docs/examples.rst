Examples
========

Examples are opt-in workflows that demonstrate MIAOW features and model integrations. Select and build them through the repository build as described in :ref:`building-examples`, then run them from their own ``examples/<example_name>`` directories:

.. code-block:: sh

   cd examples/00_hello
   ./miaow_hello
   cd ../01_heat
   ./miaow_heat
   cd ../03_dynamic_bmi
   ./miaow_dynamic_bmi --output_root ./output --max_workers 2
   cd ../07_job_controller
   ./miaow_job_controller
   cd ../09_bmi_operations
   ./miaow_bmi_operations
   cd ../10_model_coupling
   ./miaow_model_coupling --max_workers 2

Default examples
----------------

- `examples/00_hello <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/00_hello>`_: minimal orchestration and diagnostics output
- `examples/01_heat <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/01_heat>`_: BMI integration using the `CSDMS heat example <https://github.com/csdms/bmi-example-cxx>`_
- `examples/03_dynamic_bmi <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/03_dynamic_bmi>`_: C++ and Fortran heat plugins, each run in-process and in its own unit subprocess
- `examples/07_job_controller <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/07_job_controller>`_: computes a square root using Heron's method, with the executor retaining its estimate across tasks and the job controller requesting iterations until the residual meets the convergence tolerance
- `examples/09_bmi_operations <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/09_bmi_operations>`_: a controller submits initialization, time queries, two updates, and finalization as separate tasks on one static heat model
- `examples/10_model_coupling <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/10_model_coupling>`_: a task DAG transfers temperature snapshots between two retained C++ heat models over three time steps by default

The hello, dynamic BMI, job-controller, and model-coupling examples accept an optional ``--config <file.json>`` alongside the common runtime options. Omitted fields retain the default inputs.

.. list-table:: Optional JSON inputs
   :header-rows: 1
   :widths: 25 75

   * - Example
     - Fields
   * - ``00_hello``
     - ``delay_ms`` sets the delay before each task executes. ``tasks`` replaces the default messages with a list of objects containing optional ``message``, ``feature_id``, and ``task_executor_id`` fields. Each entry declares a unit; ``task_executor_id`` selects its executor registration. An omitted message produces a failed task.
   * - ``03_dynamic_bmi``
     - ``cpp_plugin``, ``fortran_plugin``, ``init_config``, and ``time_step_tolerance_seconds`` override the plugin paths and shared model configuration.
   * - ``07_job_controller``
     - ``radicand``, ``initial_estimate``, ``residual_tolerance``, and ``maximum_iterations`` set the iteration inputs and stopping conditions.
   * - ``10_model_coupling``
     - ``number_of_steps`` sets the non-negative number of coupling time steps (default: 3).

For example, a job controller config containing ``{"maximum_iterations": 1}`` reports non-convergence after the first iteration. Run it with ``./miaow_job_controller --config <file.json>`` from the example directory. Plugin and model configuration paths are also resolved from the example's working directory.

Additional examples
-------------------

- `examples/06_fuse_bmi <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/06_fuse_bmi>`_: deterministic 256-member Saltelli ensemble using the `FUSE <https://github.com/uofs-simlab/fuse>`_ dynamic Fortran BMI plugin
- `examples/08_particle_swarm <https://github.com/uofs-simlab/miaow-framework/tree/main/examples/08_particle_swarm>`_: compares batch-synchronous and asynchronous particle swarm optimization on Rastrigin, with randomized evaluation delays and Matplotlib playback

FUSE
^^^^

The ``examples/06_fuse_bmi`` example runs a deterministic 256-member Saltelli ensemble through FUSE's dynamic Fortran BMI plugin. It requires NetCDF Fortran and Python 3 with no additional packages. The NetCDF parameter files are tracked in ``examples/06_fuse_bmi/data/paramsets``, while CMake locates or downloads the forcing, elevation-band, and settings data. This case is not part of the default example set.

To run up to six model evaluations concurrently, use these commands from the repository root:

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

The synchronous job runs to completion first, then the asynchronous job starts. Both start from identical positions and velocities, using 32 particle units and a limit of eight concurrent evaluations by default. Each stops submitting work after 30 seconds or 100,000 evaluations; queued and active evaluations finish normally.

Select, build, and run this additional example from the repository root:

.. code-block:: sh

   cmake -S . -B build -DBUILD_EXAMPLES=ON -DMIAOW_EXAMPLES=08_particle_swarm
   cmake --build build --target miaow_particle_swarm
   cd examples/08_particle_swarm
   ./miaow_particle_swarm

Results are saved under ``output/run_<id>``. To compare particle histories, unit activity, and convergence, use the provided Python script:

.. code-block:: sh

   python3 -m venv .venv
   .venv/bin/python -m pip install -r requirements.txt
   .venv/bin/python animate.py "output/run_<id>"

Controller output contains particle-position snapshots and evaluation rows with task IDs and objective values. The plotting script joins these observations to the profile by task ID for dispatch and completion times and unit identity. Profile offsets use integer nanoseconds; overall runtime uses seconds.

Replace ``<id>`` with the run number in the path printed by the executable (``0`` for the first run). Figures are written to its ``figures`` subdirectory; MP4 export requires FFmpeg.

Example command-line options
----------------------------

Examples accept the following runtime command-line options where applicable. The table shows ``RuntimeOptions`` defaults; individual examples may override them. For example, ``08_particle_swarm`` defaults to eight concurrent tasks and an output root of ``output``.

.. list-table::
   :header-rows: 1
   :widths: 30 30 50

   * - Option
     - Runtime default
     - Description
   * - ``--max_workers``
     - ``1``
     - Maximum occupied task slots per job; unit count is independent of this limit. Timed-out native calls retain their slots until they return.
   * - ``--task_request_timeout``
     - ``60``
     - Task-response timeout, in seconds; expiry makes the unit unavailable and notifies the controller. Only subprocess tasks are terminated; running in-process evaluations are not interrupted.
   * - ``--job_request_timeout``
     - ``300``
     - Job timeout, in seconds; unfinished tasks fail and unit shutdown is requested. Delivery waits for cleanup, including running in-process evaluations, which are not interrupted.
   * - ``--output_root``
     - ``""``
     - Root directory path for runtime-managed output folders, disabled by default

Public CLI parsers throw ``std::runtime_error`` for unrecognized arguments, missing option values, and invalid option values. Example executables catch these errors, print the message, and exit with status ``1``.
