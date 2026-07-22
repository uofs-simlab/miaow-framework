MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing
===============================================================================

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. `MIAOW <https://github.com/uofs-simlab/miaow-framework>`_ embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW targets the `C++20 standard <https://en.cppreference.com/w/cpp/20.html>`_, uses the `C++ Actor Framework (CAF) <https://www.actor-framework.org/>`_ as its actor backend, relies on `Boost <https://www.boost.org/>`_ for process management and JSON support, and includes C++/Fortran `Basic Model Interface (BMI) <https://bmi.csdms.io/en/stable/>`_ integration paths.

Documentation
-------------

.. toctree::
   :maxdepth: 2

   architecture
   bmi
   examples
   building
   api

Contributing
------------

Contributions from the community are welcome. To report a bug, suggest an improvement, or discuss a contribution, `open an issue <https://github.com/uofs-simlab/miaow-framework/issues>`_. Code and documentation contributions can be proposed through a pull request.

Authors and funding
-------------------

MIAOW was developed with funding support from the `Cooperative Institute for Research to Operations in Hydrology (CIROH) <https://ciroh.ua.edu/>`_ and is maintained by the `Numerical Simulation Laboratory <https://github.com/uofs-simlab>`_ at the `University of Saskatchewan <https://www.usask.ca/>`_. See the `AUTHORS <https://github.com/uofs-simlab/miaow-framework/blob/main/AUTHORS.md>`_ file for a full list of individual contributors to the project.

License
-------

MIAOW is available under the `MIT License <https://github.com/uofs-simlab/miaow-framework/blob/main/LICENSE>`_.
