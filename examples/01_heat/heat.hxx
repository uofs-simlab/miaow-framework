#ifndef HEAT_INCLUDED
#define HEAT_INCLUDED

namespace heat {
  class Heat {
    private:
      double alpha;

      double **temp_z;

      void _initialize_arrays(void);

    public:
      int shape[2];
      double spacing[2];
      double origin[2];
      double time;
      double t_end;
      double dt;
      double **z;

      Heat();
      Heat(std::string config_file);
      ~Heat();
      void advance_in_time ();
  };
};

#endif
