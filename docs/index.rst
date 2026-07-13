MIAOW: Model-Independent Actor Orchestration Workflows for Scientific Computing
===============================================================================

Managing concurrent numerical simulation tasks with dynamic workloads and heterogeneous hardware can feel like herding cats. `MIAOW <https://github.com/uofs-simlab/miaow-framework>`_ embraces that reality by using actor-based asynchronous orchestration so tasks run when they are ready, maximizing utilization across available resources.

MIAOW targets the `C++20 standard <https://en.cppreference.com/w/cpp/20.html>`_, uses the `C++ Actor Framework (CAF) <https://www.actor-framework.org/>`_ as its actor backend, relies on `Boost <https://www.boost.org/>`_ for process management and JSON support, and includes C++/Fortran `Basic Model Interface (BMI) <https://bmi.csdms.io/en/stable/>`_ integration paths.

Getting started
---------------

See :doc:`development` for requirements, build options, and testing. See :doc:`examples` for runnable workflows and model-specific setup.

Documentation
-------------

.. toctree::
   :maxdepth: 2

   architecture
   bmi
   examples
   development
   api

Authors and funding
-------------------

MIAOW was developed at the University of Saskatchewan with funding support from the `Cooperative Institute for Research to Operations in Hydrology (CIROH) <https://ciroh.ua.edu/>`_.
