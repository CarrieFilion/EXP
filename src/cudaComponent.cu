// -*- C++ -*-

#include "expand.h"
#include "Component.H"
#include "cudaParticle.cuH"

#include <thrust/transform_reduce.h>
#include <thrust/functional.h>
#include <thrust/generate.h>
#include <thrust/sequence.h>
#include <thrust/reduce.h>

#include <boost/make_shared.hpp>

unsigned Component::cudaStreamData::totalInstances=0;

using PII=std::pair<int, int>;

struct testCountLevel :  public thrust::unary_function<cudaParticle, int>
{
  int _l;

  __host__ __device__
  testCountLevel(int l) : _l(l) {}

  __host__ __device__
  int operator()(const cudaParticle& p) const
  {
    if (p.lev[0] == _l) return 1;
    return 0;
  }
};

struct testCountLevel2 :  public thrust::unary_function<int, int>
{
  int _l;

__host__ __device__
  testCountLevel2(int l) : _l(l) {}

  __host__ __device__
  int operator()(const int p) const
  {
    if (p == _l) return 1;
    return 0;
  }
};

Component::cudaStreamData::cudaStreamData()
{
  // Not sure why this breaks thrust, but it does . . .
  /*
  cuda_safe_call(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking),
		 __FILE__, __LINE__,
		 "Component::cudaStreamData: error creating stream");
  */

  // Need blocking until thrust bug in binary search is fixed
  cuda_safe_call(cudaStreamCreate(&stream),
		 __FILE__, __LINE__,
		 "Component::cudaStreamData: error creating stream");
  instance = totalInstances++;
}

Component::cudaStreamData::~cudaStreamData()
{
  cuda_safe_call(cudaStreamDestroy(stream), __FILE__, __LINE__,
		 "Component::cudaStreamData: error destroying stream");
  totalInstances--;
}

void Component::cuda_initialize()
{
  cuStream = boost::make_shared<cudaStreamData>();
}


// Comparison operator for [from, to] pairs
struct pairLess
{
  __host__ __device__
  bool operator()(const thrust::pair<int, int>& lhs,
		  const thrust::pair<int, int>& rhs) const
  {
    return lhs.first < rhs.first or (lhs.first == rhs.first and lhs.second < rhs.second);
  }
};


Component::I2vec Component::CudaSortLevelChanges()
{
  // The plan: for the current active level search above and below for
  // particles for correction to coefficient matrix
  //
  // 1. Sort all particles by current level
  // 2. Get indices to range for each level L
  // 3. Within each level L, compute the ranges for changes,
  //    delta L = [-L, multistep-L]
  // 4. For each (L, delta L), compute the coefficient changes and
  //    apply to the appropriate coefficient matrices

  I2vec ret(multistep+1);
  for (auto & v : ret) v.resize(multistep+1);

  // Particle number
  //
  auto N = cuStream->cuda_particles.size();

  // Resize pair list and pair index
  //
  cuStream->levPair.resize(N);
  cuStream->indx2.  resize(N);

  try {
    auto exec = thrust::cuda::par.on(cuStream->stream);
    
    thrust::device_vector<cudaParticle>::iterator
      pbeg = cuStream->cuda_particles.begin(),
      pend = cuStream->cuda_particles.end();
    
    // This gets a vector of pairs [current index, desired index],
    // leaving the order of particle structures unchanged
    //
    if (thrust_binary_search_workaround) {
      cudaStreamSynchronize(cuStream->stream);
      thrust::transform(pbeg, pend, cuStream->levPair.begin(), cuPartToChange());
    } else {
      thrust::transform(exec, pbeg, pend, cuStream->levPair.begin(), cuPartToChange());
    }
    
    // Make the initial sequential index
    //
    thrust::sequence(cuStream->indx2.begin(), cuStream->indx2.end(), 0, 1);
  
    // Sort the keys and make the index
    //
    thrust::sort_by_key(cuStream->levPair.begin(), cuStream->levPair.end(),
			cuStream->indx2.begin());

    // This will be [from, to] pair for determining the change matrix
    //
    thrust::pair<int, int> tr2;

    for (int target=0; target<=multistep; target++) {

      // From level is 'target'
      //
      tr2.first = target;

      for (int del=0; del<=multistep; del++) {

	if (del==target) {
	  ret[target][del] = {0, 0};
	  continue;
	}
	
	// To level is 'del'
	//
	tr2.second = del;

	thrust::device_vector<thrust::pair<int, int>>::iterator
	  lbeg = cuStream->levPair.begin(), lo;

	thrust::device_vector<thrust::pair<int, int>>::iterator
	  lend = cuStream->levPair.end(),   hi;

	// Determine upper and lower indices into indx2 for the [from,
	// to] pair
	//
	if (thrust_binary_search_workaround) {
	  cudaStreamSynchronize(cuStream->stream);
	  lo  = thrust::lower_bound(lbeg, lend, tr2, pairLess());
	} else {
	  lo = thrust::lower_bound(exec, lbeg, lend, tr2, pairLess());
	}
	
	cudaStreamSynchronize(cuStream->stream);

	if (thrust_binary_search_workaround) {
	  hi = thrust::upper_bound(lbeg, lend, tr2);
	} else {
	  hi = thrust::upper_bound(exec, lbeg, lend, tr2);
	}

	cudaStreamSynchronize(cuStream->stream);

	ret[target][del] = {thrust::distance(lbeg, lo), 
			    thrust::distance(lbeg, hi)};
      }
    }

  }
  catch(std::bad_alloc &e) {
    std::cerr << "Ran out of memory while sorting" << std::endl;
    exit(-1);
  }
  catch(thrust::system_error &e) {
    std::cerr << "Some other error happened during sort, lower_bound, or upper_bound:" << e.what() << std::endl;
    exit(-1);
  }
 
  // Debugging output for level changes
  //
  if (false) {
    std::cout << std::string(15*(multistep+1), '-') << std::endl;
    std::cout << "--- " << name << " [" << myid << "]" << std::endl;
    std::cout << std::string(15*(multistep+1), '-') << std::endl;
    for (int m1=0; m1<=multistep; m1++) {
      for (int m2=0; m2<=multistep; m2++) {
	std::cout << std::setw(15) << ret[m1][m2].second - ret[m1][m2].first;
      }
      std::cout << std::endl;
    }
    std::cout << std::string(15*(multistep+1), '-') << std::endl;
  }

  return ret;
}


void Component::CudaSortByLevel()
{
  try {
    auto exec = thrust::cuda::par.on(cuStream->stream);
    
    // Convert from cudaParticle to a flat vector of levels.  The
    // order of the particle structures will remain fixed
    //
    cuStream->levList.resize(cuStream->cuda_particles.size());

    thrust::device_vector<cudaParticle>::iterator
      pbeg = cuStream->cuda_particles.begin(),
      pend = cuStream->cuda_particles.end();

    if (thrust_binary_search_workaround) {
      cudaStreamSynchronize(cuStream->stream);
      thrust::transform(pbeg, pend, cuStream->levList.begin(), cuPartToLevel());
    } else {
      thrust::transform(exec, pbeg, pend, cuStream->levList.begin(), cuPartToLevel());
    }

    // Make room for an index
    //
    cuStream->indx1.resize(cuStream->cuda_particles.size());

    // Make the initial sequential index
    //
    thrust::sequence(cuStream->indx1.begin(), cuStream->indx1.end(), 0, 1);
  
    // First sort the keys and indices by the keys.  This gives a
    // indirect index back to the particles and a sorted levList for
    // determining the partition of the indirect index into levels
    //
    thrust::sort_by_key(cuStream->levList.begin(), cuStream->levList.end(),
			cuStream->indx1.begin());
  }
  catch(thrust::system_error &e) {
    std::cerr << "Some other error happened during sort, lower_bound, or upper_bound:" << e.what() << std::endl;
    exit(-1);
  }
}



std::pair<unsigned int, unsigned int>
Component::CudaGetLevelRange(int minlev, int maxlev)
{
  std::pair<unsigned, unsigned> ret;

  try {
    auto exec = thrust::cuda::par.on(cuStream->stream);

    // Get unsigned from input
    //
    unsigned int minl = static_cast<unsigned>(minlev);
    unsigned int maxl = static_cast<unsigned>(maxlev);

    thrust::device_vector<int>::iterator lbeg = cuStream->levList.begin();
    thrust::device_vector<int>::iterator lend = cuStream->levList.end();
    thrust::device_vector<int>::iterator lo, hi;

    if (thrust_binary_search_workaround) {
      cudaStreamSynchronize(cuStream->stream);
      lo = thrust::lower_bound(lbeg, lend, minl);
    } else {
      lo = thrust::lower_bound(exec, lbeg, lend, minl);
    }
	
    if (thrust_binary_search_workaround) {
      cudaStreamSynchronize(cuStream->stream);
      hi = thrust::upper_bound(lbeg, lend, maxl);
    } else {
      hi = thrust::upper_bound(exec, lbeg, lend, maxl);
    }

    ret.first  = thrust::distance(lbeg, lo);
    ret.second = thrust::distance(lbeg, hi);

    if (false) {
      thrust::host_vector<int> testH(cuStream->levList);
      for (int n=0; n<10; n++) std::cout << " " << testH[n];
      std::cout << std::endl;

      std::cout << "Number of zeros="
		<< thrust::transform_reduce(cuStream->cuda_particles.begin(),
					    cuStream->cuda_particles.end(),
					    testCountLevel(0),
					    0, thrust::plus<int>())
		<< ", "
		<< thrust::transform_reduce(cuStream->levList.begin(),
					    cuStream->levList.end(),
					    testCountLevel2(0),
					    0, thrust::plus<int>())
		<< " lower=" << ret.first << " upper=" << ret.second
		<< std::endl;
    }
  }
  catch(thrust::system_error &e) {
    std::cerr << "Some other error happened during sort, lower_bound, or upper_bound:" << e.what() << std::endl;
    exit(-1);
  }
 
  return ret;
}

void Component::ParticlesToCuda(PartMap::iterator beg, PartMap::iterator fin)
{
  if (step_timing and use_cuda) comp->timer_cuda.start();

  auto npart = std::distance(beg, fin);
  
  // Allocate particle memory and iterators
  //
  if (host_particles.capacity()<npart) host_particles.reserve(npart);
  host_particles.resize(npart);

  cuStream->first = host_particles.begin();
  cuStream->last  = host_particles.end();

  // Translate the EXP particle to Cuda particle structures
  //
  hostPartItr hit = host_particles.begin();
  for (auto pit=beg; pit!=fin; pit++) {
    ParticleHtoD(pit->second, *(hit++));
  }

  if (step_timing and use_cuda) comp->timer_cuda.stop();
}

void Component::HostToDev(Component::cuSharedStream cr)
{
  auto npart = thrust::distance(cr->first, cr->last);
  
  if (npart) {		  // Don't bother trying to copy zero particles

    // Resize the device array, if necessary
    //
    if (cr->cuda_particles.capacity()<npart) cr->cuda_particles.reserve(npart);
    cr->cuda_particles.resize(npart);
  
    // Copy the cuda particle structures to the device
    //
    cudaMemcpyAsync(thrust::raw_pointer_cast(&cr->cuda_particles[0]),
		    thrust::raw_pointer_cast(&(*cr->first)),
		    npart*sizeof(cudaParticle),
		    cudaMemcpyHostToDevice, cr->stream);
  }

  // Make the level index after a particle copy to device
  //
  CudaSortByLevel();
}

void Component::DevToHost(Component::cuSharedStream cr)
{
  auto npart = thrust::distance(cr->first, cr->last);
  
  if (npart) {		  // Don't bother trying to copy zero particles

    cudaMemcpyAsync(thrust::raw_pointer_cast(&(*cr->first)),
		    thrust::raw_pointer_cast(&cr->cuda_particles[0]),
		    npart*sizeof(cudaParticle),
		    cudaMemcpyDeviceToHost, cr->stream);
  }
}


void Component::CudaToParticles(hostPartItr beg, hostPartItr end)
{
  if (step_timing and use_cuda) comp->timer_cuda.start();

  // DEBUG PRINTING (enable by setting imax>0)
  //
  const int imax = 0;
  int icnt = 0;

  // Translate the Cuda particle to the EXP particle structures
  //
  for (hostPartItr v=beg; v!=end; v++) {
    cudaParticle & p = *v;
    if (icnt < imax) {
      std::cout << "[" << icnt++ << ", " << myid << "] " << p << std::endl;
    }
    ParticleDtoH(p, particles[p.indx]);
  }

  if (step_timing and use_cuda) comp->timer_cuda.stop();
}

// No longer used because we need to deal with indirection
//
struct cudaZeroAcc : public thrust::unary_function<cudaParticle, cudaParticle>
{
  __host__ __device__
  cudaParticle operator()(cudaParticle& p)
  {
    for (size_t k=0; k<3; k++) p.acc[k] = 0.0;
    p.pot = p.potext = 0.0;
    return p;
  }
};

__global__ void
zeroPotAccKernel(dArray<cudaParticle> P, dArray<int> I, int stride, PII lohi)
{
  const int tid = blockDim.x * blockIdx.x + threadIdx.x;

  for (int n=0; n<stride; n++) {
    int i     = tid*stride + n;	// Index in the stride
    int npart = i + lohi.first;	// Particle index

    if (npart < lohi.second) {

      cudaParticle & p = P._v[I._v[npart]];
      
      for (int k=0; k<3; k++) p.acc[k] = 0.0;
      p.pot = p.potext = 0.0;

    } // Particle index block
    
  } // END: stride loop
}


void Component::ZeroPotAccel(int minlev)
{
  size_t psize  = particles.size();
  
  std::pair<unsigned int, unsigned int> lohi, cur;

  if (multistep)
    lohi = CudaGetLevelRange(minlev, multistep);
  else
    lohi = {0, cuStream->cuda_particles.size()};
    
  unsigned int Ntotal = lohi.second - lohi.first;
  unsigned int Npacks = Ntotal/bunchSize + 1;

  cudaDeviceProp deviceProp;
  cudaGetDeviceProperties(&deviceProp, cudaDevice);

  // Loop over bunches
  //
  for (int n=0; n<Npacks; n++) {

    // Current bunch
    //
    cur. first = lohi.first + bunchSize*n;
    cur.second = lohi.first + bunchSize*(n+1);
    cur.second = std::min<unsigned int>(cur.second, lohi.second);
    
    if (cur.second <= cur.first) break;
    
    // Compute grid
    //
    unsigned int N         = cur.second - cur.first;
    unsigned int stride    = N/BLOCK_SIZE/deviceProp.maxGridSize[0] + 1;
    unsigned int gridSize  = N/BLOCK_SIZE/stride;
      
    if (N > gridSize*BLOCK_SIZE*stride) gridSize++;
      

    // Pack the com values into a matrix, one particle per row
    // 
    zeroPotAccKernel<<<gridSize, BLOCK_SIZE, 0, cuStream->stream>>>
      (toKernel(cuStream->cuda_particles), toKernel(cuStream->indx1),
       stride, cur);
  }
  
}


__global__ void comKernel
(dArray<cudaParticle> P, dArray<int> I, dArray<cuFP_t> com,
 int stride, PII lohi)
{
  const int tid   = blockDim.x * blockIdx.x + threadIdx.x;

  for (int n=0; n<stride; n++) {
    int i     = tid*stride + n;
    int npart = i + lohi.first;

    if (npart < lohi.second) {

      cudaParticle & p = P._v[I._v[npart]];
    
      com._v[i*10+0] = p.mass;
      for (int k=0; k<3; k++) {
	com._v[i*10+1+k] = p.pos[k];
	com._v[i*10+4+k] = p.vel[k];
	com._v[i*10+7+k] = p.acc[k];
      }
    }
  }
}


// Convert linear index to row index for column reduction
//
template <typename T>
struct linear_index_to_row_index : public thrust::unary_function<T,T> {

  T Ncols; // --- Number of columns
  
  __host__ __device__ linear_index_to_row_index(T Ncols) : Ncols(Ncols) {}
  
  __host__ __device__ T operator()(T i) { return i / Ncols; }
};

void Component::fix_positions_cuda(unsigned mlevel)
{
  const int maxBunch = 40000;

				// Zero center
  for (int i=0; i<3; i++) center[i] = 0.0;

  				// Zero variables
  mtot = 0.0;
  for (int k=0; k<dim; k++) com[k] = cov[k] = coa[k] = 0.0;

				// Zero multistep counters at and
				// above this level
  try {
    auto exec = thrust::cuda::par.on(cuStream->stream);
    
    for (int mm=mlevel; mm<=multistep; mm++) {

      cudaStreamSynchronize(cuStream->stream);

      thrust::device_vector<int>::iterator
	lbeg = cuStream->levList.begin(), lo,
	lend = cuStream->levList.end(),   hi;

      if (thrust_binary_search_workaround) {
	cudaStreamSynchronize(cuStream->stream);
	lo  = thrust::lower_bound(lbeg, lend, mm);
      } else {
	lo = thrust::lower_bound(exec, lbeg, lend, mm);
      }
      
      cudaStreamSynchronize(cuStream->stream);

      if (thrust_binary_search_workaround) {
	hi = thrust::upper_bound(lbeg, lend, mm);
      } else {
	hi = thrust::upper_bound(exec, lbeg, lend, mm);
      }
      
      // Sort particles and get coefficient size
      //
      PII lohi = {thrust::distance(lbeg, lo), thrust::distance(lbeg, hi)};
      PII cur;
  
      unsigned int Ntotal = thrust::distance(lo, hi);
      unsigned int Npacks = Ntotal/maxBunch + 1;

      com_mas[mm] = 0.0;
      for (unsigned k=0; k<3; k++)  {
	com_lev[3*mm+k] = 0.0;
	cov_lev[3*mm+k] = 0.0;
	coa_lev[3*mm+k] = 0.0;
      }

      cudaDeviceProp deviceProp;
      cudaGetDeviceProperties(&deviceProp, cudaDevice);

      // Loop over bunches
      //
      for (int n=0; n<Npacks; n++) {

	// Current bunch
	//
	cur. first = lohi.first + maxBunch*n;
	cur.second = lohi.first + maxBunch*(n+1);
	cur.second = std::min<unsigned int>(cur.second, lohi.second);
	
	if (cur.second <= cur.first) break;
    
	// Compute grid
	//
	unsigned int N         = cur.second - cur.first;
	unsigned int stride    = N/BLOCK_SIZE/deviceProp.maxGridSize[0] + 1;
	unsigned int gridSize  = N/BLOCK_SIZE/stride;
    
	if (N > gridSize*BLOCK_SIZE*stride) gridSize++;

	// Resize storage as needed
	//
	const int Ncols = 10;	// mass, pos, vel, acc
	thrust::device_vector<cuFP_t> ret(N*Ncols);
	
	// Allocate space for row sums and indices
	//
	thrust::device_vector<cuFP_t> d_col_sums   (Ncols);
	thrust::device_vector<int>    d_col_indices(Ncols);


	// Pack the com values into a matrix, one particle per row
	// 
	comKernel<<<gridSize, BLOCK_SIZE, 0, cuStream->stream>>>
	  (toKernel(cuStream->cuda_particles), toKernel(cuStream->indx1),
	   toKernel(ret), stride, cur);

	// Perform sum over columns by summing values with equal column indices
	//
	thrust::reduce_by_key
	  (thrust::make_transform_iterator(thrust::counting_iterator<int>(0), linear_index_to_row_index<int>(N)),
	   thrust::make_transform_iterator(thrust::counting_iterator<int>(0), linear_index_to_row_index<int>(N)) + (N*Ncols),
	   thrust::make_permutation_iterator
	   (ret.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0),(thrust::placeholders::_1 % N) * Ncols + thrust::placeholders::_1 / N)),
	   d_col_indices.begin(),
	   d_col_sums.begin(),
	   thrust::equal_to<int>(),
	   thrust::plus<cuFP_t>());

	// Sum the partial results
	//
	com_mas[mm] += d_col_sums[0];
	for (unsigned k=0; k<3; k++)  {
	  com_lev[3*mm+k] += d_col_sums[1+k];
	  cov_lev[3*mm+k] += d_col_sums[4+k];
	  coa_lev[3*mm+k] += d_col_sums[7+k];
	}
      }
    }
  }
  catch(thrust::system_error &e) {
    std::cerr << "Some other error happened during sort, lower_bound, or upper_bound:" << e.what() << std::endl;
    exit(-1);
  }
 
  std::vector<double> com1(3, 0.0), cov1(3, 0.0), coa1(3, 0.0);
  double              mtot1 = 0.0;

  for (unsigned mm=0; mm<=multistep; mm++) {
    for (int k=0; k<3; k++) {
      com1[k] += com_lev[3*mm + k];
      cov1[k] += cov_lev[3*mm + k];
      coa1[k] += coa_lev[3*mm + k];
    }
    mtot1 += com_mas[mm];
  }

  MPI_Allreduce(&mtot1, &mtot, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&com1[0], com, 3, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&cov1[0], cov, 3, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&coa1[0], coa, 3, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    
  if (VERBOSE>5) {
				// Check for NaN
    bool com_nan = false, cov_nan = false, coa_nan = false;
    for (int k=0; k<3; k++)
      if (std::isnan(com[k])) com_nan = true;
    for (int k=0; k<3; k++)
      if (std::isnan(cov[k])) cov_nan = true;
    for (int k=0; k<3; k++)
      if (std::isnan(coa[k])) coa_nan = true;
    if (com_nan && myid==0)
      cerr << "Component [" << name << "] com has a NaN" << endl;
    if (cov_nan && myid==0)
      cerr << "Component [" << name << "] cov has a NaN" << endl;
    if (coa_nan && myid==0)
      cerr << "Component [" << name << "] coa has a NaN" << endl;
  }
				// Compute component center of mass and
				// center of velocity, and center of accel

  if (mtot > 0.0) {
    for (int k=0; k<dim; k++) com[k]  /= mtot;
    for (int k=0; k<dim; k++) cov[k]  /= mtot;
    for (int k=0; k<dim; k++) coa[k]  /= mtot;
  }

  if (com_system and not consp) {
    for (int k=0; k<dim; k++) com0[k] = com[k];
    for (int k=0; k<dim; k++) cov0[k] = cov[k];
  }

  if (com_system) {	   // Use local center of accel for com update
    for (int k=0; k<dim; k++) acc0[k]  = coa[k];
  } else {			// No mass, no acceleration?
    for (int k=0; k<dim; k++) acc0[k]  = 0.0;
  }

  if ((EJ & Orient::CENTER) && !EJdryrun) {
    Vector ctr = orient->currentCenter();
    bool ok    = true;
    for (int i=0; i<3; i++) {
      if (std::isnan(ctr[i+1])) ok = false;
    } 
    if (ok) {
      for (int i=0; i<3; i++) center[i] += ctr[i+1];
    } else if (myid==0) {
      cout << "Orient: center failure, T=" << tnow 
	   << ", adjustment skipped" << endl;
    }
  }
}


void Component::print_level_lists_cuda(double T)
{
				// Retrieve counts per level
  std::vector<int> cntr(multistep+1);
  for (int m=0; m<=multistep; m++) {
    cntr[m] = thrust::transform_reduce(cuStream->cuda_particles.begin(),
				       cuStream->cuda_particles.end(),
				       testCountLevel(m),
				       0, thrust::plus<int>());
  }

  if (myid==0) {
				// Sum reduce to root
    MPI_Reduce(MPI_IN_PLACE, &cntr[0], multistep+1, MPI_INT, MPI_SUM,
	       0, MPI_COMM_WORLD);

    int tot=0;
    for (int m=0; m<=multistep; m++) tot += cntr[m];

    if (tot) {

      std::ostringstream ofil;
      ofil << runtag << ".levels";
      std::ofstream out(ofil.str().c_str(), ios::app);

      int sum=0;
      out << setw(60) << setfill('-') << '-' << endl;
      std::ostringstream sout;
      sout << "--- Component <" << name 
	   << ", " << id  << ">, T=" << T;
      out << std::setw(60) << std::left << sout.str().c_str() << std::endl;
      out << std::setw(60) << '-' << std::endl << std::setfill(' ');
      out << std::setw(3)  << "L" 
	  << std::setw(10) << "Number" 
	  << std::setw(10) << "dN/dL" 
	  << std::setw(10) << "N(<=L)"
	  << std::endl
	  << std::setw(60) << std::setfill('-') << '-'
	  << std::endl << std::setfill(' ');
      for (int n=0; n<=multistep; n++) {
	sum += cntr[n];
	out << std::setw(3)  << n 
	    << std::setw(10) << cntr[n] << std::setprecision(3) << std::fixed
	    << std::setw(10) << static_cast<double>(cntr[n])/tot
	    << std::setw(10) << static_cast<double>(sum)    /tot;
	out << std::endl;
      }
      out << std::endl << std::setw(3) << "T" << std::setw(10) << tot
	  << std::endl << std::endl << std::right;
    } else {
      std::cout << "print_level_lists_cuda [" << name 
		<< ", T=" << tnow << "]: tot=" << tot << std::endl;
    }

  } else {
				// Sum reduce counts to root
    MPI_Reduce(&cntr[0], 0, multistep+1, MPI_INT, MPI_SUM,
	       0, MPI_COMM_WORLD);
  }

}

// No cuda code here but only used after CudaToParticles() call for
// testing
void Component::MakeLevlist()
{
  levlist.resize(multistep+1);
  for (auto & v : levlist) v.clear();
  for (auto & v : particles) levlist[v.second->level].push_back(v.first);
}
