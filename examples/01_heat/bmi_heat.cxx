#include <stdio.h>

#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "heat.hxx"
#include "third_party/bmi/bmi.hxx"
#include "bmi_heat.hxx"


void BmiHeat::
Initialize (std::string config_file)
{
  if (config_file.compare("") != 0 )
    this->_model = heat::Heat(config_file);
}


void BmiHeat::
Update()
{
  this->_model.advance_in_time();
}


void BmiHeat::
UpdateUntil(double t)
{
  double time;
  double dt;

  time = this->GetCurrentTime();
  dt = this->GetTimeStep();

  {
    double n_steps = (t - time) / dt;
    double frac;

    for (int n=0; n<int(n_steps); n++)
      this->Update();

    frac = n_steps - int(n_steps);
    this->_model.dt = frac * dt;
    this->_model.advance_in_time();
    this->_model.dt = dt;
  }
}


void BmiHeat::
Finalize()
{
  this->_model.~Heat();
}


int BmiHeat::
GetVarGrid(std::string name)
{
  if (name.compare("plate_surface__temperature") == 0)
    return 0;
  else
    return -1;
}


std::string BmiHeat::
GetVarType(std::string name)
{
  if (name.compare("plate_surface__temperature") == 0)
    return "double";
  else
    return "";
}


int BmiHeat::
GetVarItemsize(std::string name)
{
  if (name.compare("plate_surface__temperature") == 0)
    return sizeof(double);
  else
    return 0;
}


std::string BmiHeat::
GetVarUnits(std::string name)
{
  if (name.compare("plate_surface__temperature") == 0)
    return "K";
  else
    return "";
}


int BmiHeat::
GetVarNbytes(std::string name)
{
  int itemsize;
  int gridsize;

  itemsize = this->GetVarItemsize(name);
  gridsize = this->GetGridSize(this->GetVarGrid(name));
  
  return itemsize * gridsize;
}


std::string BmiHeat::
GetVarLocation(std::string name)
{
  if (name.compare("plate_surface__temperature") == 0)
    return "node";
  else
    return "";
}


void BmiHeat::
GetGridShape(const int grid, int *shape)
{
  if (grid == 0) {
    shape[0] = this->_model.shape[0];
    shape[1] = this->_model.shape[1];
  }
}


void BmiHeat::
GetGridSpacing (const int grid, double * spacing)
{
  if (grid == 0) {
    spacing[0] = this->_model.spacing[0];
    spacing[1] = this->_model.spacing[1];
  }
}


void BmiHeat::
GetGridOrigin (const int grid, double *origin)
{
  if (grid == 0) {
    origin[0] = this->_model.origin[0];
    origin[1] = this->_model.origin[1];
  }
}


int BmiHeat::
GetGridRank(const int grid)
{
  if (grid == 0)
    return 2;
  else
    return -1;
}


int BmiHeat::
GetGridSize(const int grid)
{
  if (grid == 0)
    return this->_model.shape[0] * this->_model.shape[1];
  else
    return -1;
}


std::string BmiHeat::
GetGridType(const int grid)
{
  if (grid == 0)
    return "uniform_rectilinear";
  else
    return "";
}


void BmiHeat::
GetGridX(const int grid, double *x)
{
  throw NotImplemented();
}


void BmiHeat::
GetGridY(const int grid, double *y)
{
  throw NotImplemented();
}


void BmiHeat::
GetGridZ(const int grid, double *z)
{
  throw NotImplemented();
}


int BmiHeat::
GetGridNodeCount(const int grid)
{
  if (grid == 0)
    return this->_model.shape[0] * this->_model.shape[1];
  else
    return -1;
}


int BmiHeat::
GetGridEdgeCount(const int grid)
{
  throw NotImplemented();
}


int BmiHeat::
GetGridFaceCount(const int grid)
{
  throw NotImplemented();
}


void BmiHeat::
GetGridEdgeNodes(const int grid, int *edge_nodes)
{
  throw NotImplemented();
}


void BmiHeat::
GetGridFaceEdges(const int grid, int *face_edges)
{
  throw NotImplemented();
}


void BmiHeat::
GetGridFaceNodes(const int grid, int *face_nodes)
{
  throw NotImplemented();
}


void BmiHeat::
GetGridNodesPerFace(const int grid, int *nodes_per_face)
{
  throw NotImplemented();
}


void BmiHeat::
GetValue (std::string name, void *dest)
{
  void * src = NULL;
  int nbytes = 0;

  src = this->GetValuePtr(name);
  nbytes = this->GetVarNbytes(name);

  memcpy (dest, src, nbytes);
}


void *BmiHeat::
GetValuePtr (std::string name)
{
  if (name.compare("plate_surface__temperature") == 0)
    return (void*)this->_model.z[0];
  else
    return NULL;
}


void BmiHeat::
GetValueAtIndices (std::string name, void *dest, int *inds, int len)
{
  void * src = NULL;

  src = this->GetValuePtr(name);

  if (src) {
    int i;
    int itemsize = 0;
    int offset;
    char *ptr;

    itemsize = this->GetVarItemsize(name);

    for (i=0, ptr=(char *)dest; i<len; i++, ptr+=itemsize) {
      offset = inds[i] * itemsize;
      memcpy(ptr, (char *)src + offset, itemsize);
    }
  }
}


void BmiHeat::
SetValue (std::string name, void *src)
{
  void * dest = NULL;

  dest = this->GetValuePtr(name);

  if (dest) {
    int nbytes = 0;
    nbytes = this->GetVarNbytes(name);
    memcpy(dest, src, nbytes);
  }
}


void BmiHeat::
SetValueAtIndices (std::string name, int * inds, int len, void *src)
{
  void * dest = NULL;

  dest = this->GetValuePtr(name);

  if (dest) {
    int i;
    int itemsize = 0;
    int offset;
    char *ptr;

    itemsize = this->GetVarItemsize(name);

    for (i=0, ptr=(char *)src; i<len; i++, ptr+=itemsize) {
      offset = inds[i] * itemsize;
      memcpy((char *)dest + offset, ptr, itemsize);
    }
  }
}


std::string BmiHeat::
GetComponentName()
{
  return "The 2D Heat Equation";
}


int BmiHeat::
GetInputItemCount()
{
  return this->input_var_name_count;
}


int BmiHeat::
GetOutputItemCount()
{
  return this->output_var_name_count;
}


std::vector<std::string> BmiHeat::
GetInputVarNames()
{
  std::vector<std::string> names;

  for (int i=0; i<this->input_var_name_count; i++)
    names.push_back(this->input_var_names[i]);

  return names;
}


std::vector<std::string> BmiHeat::
GetOutputVarNames()
{
  std::vector<std::string> names;

  for (int i=0; i<this->input_var_name_count; i++)
    names.push_back(this->output_var_names[i]);

  return names;
}


double BmiHeat::
GetStartTime () {
  return 0.;
}


double BmiHeat::
GetEndTime () {
  return this->_model.t_end;
}


double BmiHeat::
GetCurrentTime () {
  return this->_model.time;
}


std::string BmiHeat::
GetTimeUnits() {
  return "s";
}


double BmiHeat::
GetTimeStep () {
  return this->_model.dt;
}
