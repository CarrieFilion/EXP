// -*- C++ -*-

#include <Component.H>
#include <expand.h>
#include <cudaUtil.cuH>
#include <cudaReduce.cuH>
#include <cudaParticle.cuH>

#include <boost/make_shared.hpp>

__global__ void velocityKick
(dArray<cudaParticle> P, dArray<int> I, cuFP_t dt, int dim, int stride, PII lohi)
{
  // Thread ID
  //
  const int tid = blockDim.x * blockIdx.x + threadIdx.x;

  for (int n=0; n<stride; n++) {
    int i     = tid*stride + n;	// Particle counter
    int npart = i + lohi.first;	// Particle index

    if (npart < lohi.second) {

#ifdef BOUNDS_CHECK
      if (npart>=P._s) printf("out of bounds: %s:%d\n", __FILE__, __LINE__);
#endif
      cudaParticle * p = &P._v[I._v[npart]];
    
      for (int k=0; k<dim; k++) p->vel[k] += p->acc[k]*dt;
    }
  }
}

__global__ void velocityDebug
(dArray<cudaParticle> P, dArray<int> I, int stride, PII lohi)
{
  // Thread ID
  //
  const int tid = blockDim.x * blockIdx.x + threadIdx.x;

  for (int n=0; n<stride; n++) {
    int i     = tid*stride + n;	// Particle counter
    int npart = i + lohi.first;	// Particle index

    if (npart < lohi.second and npart < I._s) {

      cudaParticle & p = P._v[I._v[npart]];
    
      printf("%d vel a=(%13.6e %13.6e %13.6e) p=%13.6e\n", i, p.acc[0], p.acc[1], p.acc[2], p.pot);
    }
  }
}


void incr_velocity_cuda(cuFP_t dt, int mlevel)
{
  for (auto c : comp->components) {

    auto cr = c->cuStream;

    PII lohi = {0, cr->cuda_particles.size()};

    if (multistep) {		// Get particle range
      lohi = c->CudaGetLevelRange(mlevel, multistep);
    }

    cudaDeviceProp deviceProp;
    cudaGetDeviceProperties(&deviceProp, c->cudaDevice);

    // Compute grid
    //
    unsigned int N         = lohi.second - lohi.first;
    unsigned int stride    = N/BLOCK_SIZE/deviceProp.maxGridSize[0] + 1;
    unsigned int gridSize  = N/BLOCK_SIZE/stride;
    
    if (N>0) {
      
      if (N > gridSize*BLOCK_SIZE*stride) gridSize++;

      // Do the work
      //
      velocityKick<<<gridSize, BLOCK_SIZE>>>
	(toKernel(c->cuStream->cuda_particles),
	 toKernel(c->cuStream->indx1), dt, c->dim, stride, lohi);
    }

    // DEBUGGING output
    //
    if (false) {
      PII lohi(0, std::min<int>(3, cr->cuda_particles.size()));

      cudaDeviceProp deviceProp;
      cudaGetDeviceProperties(&deviceProp, c->cudaDevice);

      // Compute grid
      //
      unsigned int N         = lohi.second - lohi.first;
      unsigned int stride    = N/BLOCK_SIZE/deviceProp.maxGridSize[0] + 1;
      unsigned int gridSize  = N/BLOCK_SIZE/stride;
      
      if (N>0) {
	
	if (N > gridSize*BLOCK_SIZE*stride) gridSize++;
	
	// Do the work
	//
	velocityDebug<<<gridSize, BLOCK_SIZE>>>
	  (toKernel(cr->cuda_particles), toKernel(cr->indx1), stride, lohi);
      }
    }
    // END: DEBUG
  }
  // END: component loop
}
