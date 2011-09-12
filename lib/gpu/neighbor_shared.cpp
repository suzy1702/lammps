/***************************************************************************
                             neighbor_shared.cpp
                             -------------------
                            W. Michael Brown (ORNL)

  Class for management of data shared by all neighbor lists

 __________________________________________________________________________
    This file is part of the LAMMPS Accelerator Library (LAMMPS_AL)
 __________________________________________________________________________

    begin                : 
    email                : brownw@ornl.gov
 ***************************************************************************/

#include "precision.h"
#include "neighbor_shared.h"

#ifdef USE_OPENCL
#include "neighbor_cpu_cl.h"
#else
#include "neighbor_cpu_ptx.h"
#include "neighbor_gpu_ptx.h"
#endif

using namespace LAMMPS_AL;

void NeighborShared::clear() {
  if (_compiled) {
    if (_gpu_nbor>0) {
      if (_gpu_nbor==1) {
        k_cell_id.clear();
        k_cell_counts.clear();
      }
      k_build_nbor.clear();
      k_transpose.clear();
      k_special.clear();
      delete build_program;
    } else {
      k_nbor.clear();
      delete nbor_program;
    }
    _compiled=false;
  }
}

void NeighborShared::compile_kernels(UCL_Device &dev, const int gpu_nbor) {
  if (_compiled)
  	return;
  	
  _gpu_nbor=gpu_nbor;
  std::string flags="-cl-fast-relaxed-math -cl-mad-enable -D"+
                    std::string(OCL_VENDOR);

  if (gpu_nbor==0) {
    nbor_program=new UCL_Program(dev);
    nbor_program->load_string(neighbor_cpu,flags.c_str());
    k_nbor.set_function(*nbor_program,"kernel_unpack");
  } else {
    build_program=new UCL_Program(dev);
    #ifdef USE_OPENCL
    if (gpu_nbor==1) {
      std::cerr << "CANNOT CURRENTLY USE GPU NEIGHBORING WITH OPENCL\n";
      exit(1);
    }
    #else
    build_program->load_string(neighbor_gpu,flags.c_str());
    #endif
    if (gpu_nbor==1) {
      k_cell_id.set_function(*build_program,"calc_cell_id");
      k_cell_counts.set_function(*build_program,"kernel_calc_cell_counts");
    }
    k_build_nbor.set_function(*build_program,"calc_neigh_list_cell");
    k_transpose.set_function(*build_program,"transpose");
    k_special.set_function(*build_program,"kernel_special");
    neigh_tex.get_texture(*build_program,"neigh_tex");
  }
  _compiled=true;
}
