/***************************************************************************
                                  neighbor.h
                             -------------------
                            W. Michael Brown (ORNL)
                              Peng Wang (Nvidia)

  Class for handling neighbor lists

 __________________________________________________________________________
    This file is part of the LAMMPS Accelerator Library (LAMMPS_AL)
 __________________________________________________________________________

    begin                : 
    email                : brownw@ornl.gov, penwang@nvidia.com
 ***************************************************************************/

#ifndef LAL_NEIGHBOR_H
#define LAL_NEIGHBOR_H

#include "atom.h"
#include "neighbor_shared.h"

#define IJ_SIZE 131072

#ifdef USE_OPENCL

#include "geryon/ocl_timer.h"
#include "geryon/ocl_mat.h"
using namespace ucl_opencl;

#else

#include "geryon/nvd_timer.h"
#include "geryon/nvd_mat.h"
using namespace ucl_cudadr;

#endif

namespace LAMMPS_AL {

class Neighbor {
 public:
  Neighbor() : _allocated(false), _use_packing(false) {}
  ~Neighbor() { clear(); }
 
  /// Determine whether neighbor unpacking should be used
  /** If false, twice as much memory is reserved to allow unpacking neighbors by 
    * atom for coalesced access. **/
  void packing(const bool use_packing) { _use_packing=use_packing; }
  
  /// Clear any old data and setup for new LAMMPS run
  /** \param inum Initial number of particles whose neighbors stored on device
    * \param host_inum Initial number of particles whose nbors copied to host
    * \param max_nbors Initial number of rows in the neighbor matrix
    * \param gpu_nbor 0 if neighboring will be performed on host
    *        gpu_nbor 1 if neighboring will be performed on device
    *        gpu_nbor 2 if binning on host and neighboring on device
    * \param gpu_host 0 if host will not perform force calculations,
    *                 1 if gpu_nbor is true, and host needs a half nbor list,
    *                 2 if gpu_nbor is true, and host needs a full nbor list
    * \param pre_cut True if cutoff test will be performed in separate kernel
    *                than the force kernel **/
  bool init(NeighborShared *shared, const int inum, const int host_inum,
            const int max_nbors, const int maxspecial, UCL_Device &dev,
            const int gpu_nbor, const int gpu_host, const bool pre_cut,
            const int block_cell_2d, const int block_cell_id, 
            const int block_nbor_build);

  /// Set the size of the cutoff+skin
  inline void cell_size(const double size) { _cell_size=size; }
  
  /// Get the size of the cutoff+skin
  inline double cell_size() const { return _cell_size; }

  /// Check if there is enough memory for neighbor data and realloc if not
  /** \param inum Number of particles whose nbors will be stored on device
    * \param max_nbor Current max number of neighbors for a particle
    * \param success False if insufficient memory **/
  inline void resize(const int inum, const int max_nbor, bool &success) {
    if (inum>_max_atoms || max_nbor>_max_nbors) {
      _max_atoms=static_cast<int>(static_cast<double>(inum)*1.10);
      if (max_nbor>_max_nbors)
        _max_nbors=static_cast<int>(static_cast<double>(max_nbor)*1.10);
      alloc(success);
    }
  }

  /// Check if there is enough memory for neighbor data and realloc if not
  /** \param inum Number of particles whose nbors will be stored on device
    * \param host_inum Number of particles whose nbors will be copied to host
    * \param max_nbor Current max number of neighbors for a particle
    * \param success False if insufficient memory **/
  inline void resize(const int inum, const int host_inum, const int max_nbor, 
                     bool &success) {
    if (inum>_max_atoms || max_nbor>_max_nbors || host_inum>_max_host) {
      _max_atoms=static_cast<int>(static_cast<double>(inum)*1.10);
      _max_host=static_cast<int>(static_cast<double>(host_inum)*1.10);
      if (max_nbor>_max_nbors)
        _max_nbors=static_cast<int>(static_cast<double>(max_nbor)*1.10);
      alloc(success);
    }
  }

  /// Free all memory on host and device
  void clear();
 
  /// Bytes per atom used on device
  int bytes_per_atom(const int max_nbors) const;
  
  /// Total host memory used by class
  double host_memory_usage() const;
  
  /// Returns the type of neighboring:
  /** - 0 if neighboring will be performed on host
    * - 1 if neighboring will be performed on device
    * - 2 if binning on host and neighboring on device **/
  inline int gpu_nbor() const { return _gpu_nbor; }
  
  /// Make a copy of unpacked nbor lists in the packed storage area (for gb)
  inline void copy_unpacked(const int inum, const int maxj) 
    { ucl_copy(dev_packed,dev_nbor,inum*(maxj+2),true); }

  /// Copy neighbor list from host (first time or from a rebuild)  
  void get_host(const int inum, int *ilist, int *numj, 
                int **firstneigh, const int block_size);
  
  /// Return the stride in elements for each nbor row
  inline int nbor_pitch() const { return _nbor_pitch; }
  
  /// Return the maximum number of atoms that can currently be stored
  inline int max_atoms() const { return _max_atoms; }

  /// Return the maximum number of nbors for a particle based on current alloc
  inline int max_nbors() const { return _max_nbors; }

  /// Loop through neighbor count array and return maximum nbors for a particle
  inline int max_nbor_loop(const int inum, int *numj, int *ilist) const {
    int mn=0;
    for (int i=0; i<inum; i++)
      mn=std::max(mn,numj[ilist[i]]);
    return mn;
  }

  /// Build nbor list on the device
  template <class numtyp, class acctyp>
  void build_nbor_list(const int inum, const int host_inum, const int nall,
                       Atom<numtyp,acctyp> &atom, double *sublo,
                       double *subhi, int *tag, int **nspecial, int **special, 
                       bool &success, int &max_nbors);

  /// Return the number of bytes used on device
  inline double gpu_bytes() {
    double res = _gpu_bytes + _c_bytes + _cell_bytes;
    if (_gpu_nbor==0)
      res += 2*IJ_SIZE*sizeof(int);

    return res;
  }
  
  // ------------------------------- Data -------------------------------

  /// Device neighbor matrix
  /** - 1st row is i (index into atom data)
    * - 2nd row is numj (number of neighbors)
    * - 3rd row is starting location in packed nbors
    * - Remaining rows are the neighbors arranged for coalesced access **/
  UCL_D_Vec<int> dev_nbor;
  /// Packed storage for neighbor lists copied from host
  UCL_D_Vec<int> dev_packed;
  /// Host buffer for copying neighbor lists
  UCL_H_Vec<int> host_packed;
  /// Host storage for nbor counts (row 1) & accumulated neighbor counts (row2)
  UCL_H_Vec<int> host_acc;

  // ----------------- Data for GPU Neighbor Calculation ---------------

  /// Host storage for device calculated neighbor lists
  /** Same storage format as device matrix **/
  UCL_H_Vec<int> host_nbor;
  /// Device storage for neighbor list matrix that will be copied to host
  /** - 1st row is numj
    * - Remaining rows are by atom, columns are nbors **/
  UCL_D_Vec<int> dev_host_nbor;
  UCL_D_Vec<int> dev_host_numj;
  UCL_H_Vec<int> host_ilist;
  UCL_H_Vec<int*> host_jlist;
  /// Device storage for special neighbor counts
  UCL_D_Vec<int> dev_nspecial;
  /// Device storage for special neighbors
  UCL_D_Vec<int> dev_special, dev_special_t;

  /// Device timers
  UCL_Timer time_nbor, time_kernel;
  
 private:
  NeighborShared *_shared;
  UCL_Device *dev;
  bool _allocated, _use_packing;
  int _gpu_nbor, _max_atoms, _max_nbors, _max_host, _nbor_pitch, _maxspecial;
  bool _gpu_host, _alloc_packed;
  double _cell_size;

  double _gpu_bytes, _c_bytes, _cell_bytes;
  void alloc(bool &success);
  
  int _block_cell_2d, _block_cell_id, _block_nbor_build;
};

}

#endif
