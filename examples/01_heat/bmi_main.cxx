/* TODO: Actorify this */

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <vector>

#include "heat.hxx"
#include <third_party/bmi.hxx>
#include "bmi_heat.hxx"


int main(int argc, char *argv[])
{
  BmiHeat model;

  if (argc != 2) {
    printf("Usage: run_bmiheatcxx CONFIGURATION_FILE\n\n");
    printf("Run the heatcxx model through its BMI with a configuration file.\n");
    printf("Output is written to the file `bmiheatcxx.out`.\n");
    return bmi::BMI_SUCCESS;
  }

  FILE *fp = fopen("bmiheatcxx.out", "w");
  fprintf(fp, "Configuration file = %s\n", argv[1]);

  fprintf(fp, "Initializing... ");
  model.Initialize(argv[1]);
  fprintf(fp, "done\n");

  {
    std::string model_name;

    model_name = model.GetComponentName();
    fprintf(fp, "%s\n", model_name.c_str());
  }

  {
    std::string var_name = "plate_surface__temperature";
    int grid, rank, *shape;
    double *var = NULL;
    double time = 0.0;
    double end_time;

    fprintf(fp, "variable = %s\n", var_name.c_str());
    grid = model.GetVarGrid(var_name);

    rank = model.GetGridRank(grid);
    fprintf(fp, "rank = %d\n", rank);
    shape = new int[rank];
    model.GetGridShape(grid, shape);
    fprintf(fp, "shape = %d x %d\n", shape[0], shape[1]);

    var = (double *)model.GetValuePtr(var_name);

    end_time = model.GetEndTime();
    while (time < end_time) {
      time = model.GetCurrentTime();
      fprintf(fp, "\nTime = %f\n", time);

      {
        int i, j;
        for (i=0; i < shape[0]; i++) {
          for (j=0; j < shape[1]; j++) {
            fprintf(fp, "%6.2e ", var[i*shape[1] + j]);
          }
          fprintf(fp, "\n");
        }
      }

      model.Update();
    }
  }
  
  fprintf(fp, "Finalizing... ");
  model.Finalize();
  fprintf(fp, "done\n");

  fclose(fp);

  return bmi::BMI_SUCCESS;
}
