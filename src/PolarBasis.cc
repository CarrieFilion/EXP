#include "expand.H"

#include <filesystem>
#include <sstream>
#include <chrono>
#include <string>
#include <vector>
#include <cmath>
#include <set>

#include <PolarBasis.H>
#include <MixtureBasis.H>

// #define TMP_DEBUG
// #define MULTI_DEBUG

//@{
//! These are for testing exclusively (should be set false for production)
static bool cudaAccumOverride = false;
static bool cudaAccelOverride = false;
//@}

#ifdef DEBUG
static pthread_mutex_t io_lock;
#endif

pthread_mutex_t PolarBasis::used_lock;
pthread_mutex_t PolarBasis::cos_coef_lock;
pthread_mutex_t PolarBasis::sin_coef_lock;

bool PolarBasis::NewCoefs = true;

const std::set<std::string>
PolarBasis::valid_keys = {
  "rmin",
  "rmax",
  "self_consistent",
  "NO_M0",
  "NO_M1",
  "EVEN_M",
  "M0_ONLY",
  "NOISE",
  "noiseN",
  "noise_model_file",
  "seedN",
  "ssfrac",
  "playback",
  "coefCompute",
  "coefMaster"
};

PolarBasis::PolarBasis(Component* c0, const YAML::Node& conf, MixtureBasis *m) : 
  AxisymmetricBasis(c0, conf)
{
#if HAVE_LIBCUDA==1
  if (m) {
    throw GenericError("Error in PolarBasis: MixtureBasis logic is not yet "
		       "implemented in CUDA", __FILE__, __LINE__, 1030, false);
  }

  // Initialize the circular storage container 
  cuda_initialize();
  initialize_cuda_plr = true;

#endif

  dof              = 2;
  mix              = m;
  geometry         = cylinder;
  coef_dump        = true;
  NO_M0            = false;
  NO_M1            = false;
  EVEN_M           = false;
  M0_only          = false;
  ssfrac           = 0.0;
  subset           = false;
  coefMaster       = true;
  lastPlayTime     = -std::numeric_limits<double>::max();
#if HAVE_LIBCUDA==1
  cuda_aware       = true;
#endif

  coefs_made       = std::vector<bool>(multistep+1, false);

  // Remove matched keys
  //
  for (auto v : valid_keys) current_keys.erase(v);

  // Assign values from YAML
  //
  try {
    if (conf["rmin"]) 
      rmin = conf["rmin"].as<double>();
    else
      rmin = 0.0;

    if (conf["rmax"]) 
      rmax = conf["rmax"].as<double>();
    else
      rmax = std::numeric_limits<double>::max();

    if (conf["self_consistent"]) {
      self_consistent = conf["self_consistent"].as<bool>();
    } else
      self_consistent = true;

    if (conf["NO_M0"])   NO_M0   = conf["NO_M0"].as<bool>();
    if (conf["NO_M1"])   NO_M1   = conf["NO_M1"].as<bool>();
    if (conf["EVEN_M"])  EVEN_M  = conf["EVEN_M"].as<bool>();
    if (conf["M0_ONLY"]) M0_only = conf["M0_ONLY"].as<bool>();
    
    if (conf["ssfrac"]) {
      ssfrac = conf["ssfrac"].as<double>();
      // Check for sane value
      if (ssfrac>0.0 && ssfrac<1.0) subset = true;
    }

    if (conf["Lmax"] and not conf["Mmax"]) Mmax = Lmax;
    if (conf["Mmax"] and not conf["Lmax"]) Lmax = Mmax;
    if (Lmax != Mmax) Lmax = Mmax;

    if (conf["playback"]) {
      std::string file = conf["playback"].as<std::string>();
				// Check the file exists
      {
	std::ifstream test(file);
	if (not test) {
	  std::cerr << "PolarBasis: process " << myid << " cannot open <"
		    << file << "> for reading" << std::endl;
	  MPI_Finalize();
	  exit(-1);
	}
      }

      // This creates the Coefs instance
      playback = std::dynamic_pointer_cast<CoefClasses::CylCoefs>(CoefClasses::Coefs::factory(file));

      // Check to make sure that has been created
      if (not playback) {
	throw GenericError("PolarBasis: failure in downcasting",
			   __FILE__, __LINE__, 1031, false);
      }

      // Set tolerance to 2 master time steps
      playback->setDeltaT(dtime*2);

      if (playback->nmax() != nmax) {
	if (myid==0) {
	  std::cerr << "PolarBasis: nmax for playback [" << playback->nmax()
		    << "] does not match specification [" << nmax << "]"
		    << std::endl;
	}
	MPI_Finalize();
	exit(-1);
      }

      if (playback->mmax() != Mmax) {
	if (myid==0) {
	  std::cerr << "PolarBasis: Mmax for playback [" << playback->mmax()
		    << "] does not match specification [" << Mmax << "]"
		    << std::endl;
	}
	MPI_Finalize();
	exit(-1);
      }

      play_back = true;

      if (conf["coefCompute"]) play_cnew = conf["coefCompute"].as<bool>();

      if (conf["coefMaster"]) coefMaster = conf["coefMaster"].as<bool>();

      if (myid==0) {
	std::cout << "---- Playback is ON for Component " << component->name
		  << " using Force " << component->id << std::endl;
	if (coefMaster)
	  std::cout << "---- Playback will use MPI master" << std::endl;

	if (play_cnew)
	  std::cout << "---- New coefficients will be computed from particles on playback" << std::endl;
      }
    }

  }
  catch (YAML::Exception & error) {
    if (myid==0) std::cout << "Error parsing parameters in PolarBasis: "
			   << error.what() << std::endl
			   << std::string(60, '-') << std::endl
			   << "Config node"        << std::endl
			   << std::string(60, '-') << std::endl
			   << conf                 << std::endl
			   << std::string(60, '-') << std::endl;
    MPI_Finalize();
    exit(-1);
  }

  if (nthrds<1) nthrds=1;

  initialize();

  // Allocate coefficient matrix (one for each multistep level)
  // and zero-out contents
  //
  differ1 = vector< vector<Eigen::MatrixXd> >(nthrds);
  for (int n=0; n<nthrds; n++) {
    differ1[n] = vector<Eigen::MatrixXd>(multistep+1);
    for (int i=0; i<=multistep; i++)
      differ1[n][i].resize((2*Mmax+1), nmax);
  }

  // MPI buffer space
  //
  unsigned sz = (multistep+1)*(2*Mmax+1)*nmax;
  pack   = vector<double>(sz);
  unpack = vector<double>(sz);

  // Coefficient evaluation times
  // 
  expcoefN.resize(multistep+1);
  expcoefL.resize(multistep+1);
  for (int i=0; i<=multistep; i++) {
    expcoefN[i].resize((2*Mmax+1));
    expcoefL[i].resize((2*Mmax+1));
    for (auto & v : expcoefN[i]) {
      v = std::make_shared<Eigen::VectorXd>(nmax);
      v->setZero();
    }
    for (auto & v : expcoefL[i]) {
      v = std::make_shared<Eigen::VectorXd>(nmax);
      v->setZero();
    }
  }
    
  expcoef .resize(2*Mmax+1);
  expcoef1.resize(2*Mmax+1);
  
  for (auto & v : expcoef ) v = std::make_shared<Eigen::VectorXd>(nmax);
  for (auto & v : expcoef1) v = std::make_shared<Eigen::VectorXd>(nmax);
  
  expcoef0.resize(nthrds);
  for (auto & t : expcoef0) {
    t.resize(2*Mmax+1);
    for (auto & v : t) v = std::make_shared<Eigen::VectorXd>(nmax);
  }

  // Allocate normalization matrix

  normM.resize(Mmax+1, nmax);

  for (int m=0; m<=Mmax; m++) {
    for (int n=0; n<nmax; n++) {
      normM(m, n) = 1.0;
    }
  }

  if (pcavar) {
    muse1 = vector<double>(nthrds, 0.0);
    muse0 = 0.0;

    pthread_mutex_init(&cc_lock, NULL);
  }

  // Potential and deriv matrices
  //
  normM.resize(Mmax+1, nmax);
  krnl. resize(Mmax+1, nmax);
  dend. resize(Mmax+1, nmax);

  potd.resize(nthrds);
  dpotR.resize(nthrds);
  dpotZ.resize(nthrds);

  for (auto & v : potd) v.resize(Mmax+1, nmax);
  for (auto & v : dpotR) v.resize(Mmax+1, nmax);
  for (auto & v : dpotZ) v.resize(Mmax+1, nmax);

  // Sin, cos, legendre
  //
  cosm .resize(nthrds);
  sinm .resize(nthrds);

  for (auto & v : cosm)  v.resize(Mmax+1);
  for (auto & v : sinm)  v.resize(Mmax+1);

  // Work vectors
  //
  u. resize(nthrds);
  du.resize(nthrds);

  for (auto & v : u)  v.resize(nmax+1);
  for (auto & v : du) v.resize(nmax+1);

  firstime_coef  = true;
  firstime_accel = true;

  vc.resize(nthrds);
  vs.resize(nthrds);
  for (int i=0; i<nthrds; i++) {
    vc[i].resize(max<int>(1,Mmax)+1, nmax);
    vs[i].resize(max<int>(1,Mmax)+1, nmax);
  }

#ifdef DEBUG
  pthread_mutex_init(&io_lock, NULL);
#endif

  // Initialize storage for the multistep arrays
  //
  differC1.resize(nthrds);
  differS1.resize(nthrds);

  for (auto & v : differC1) v.resize(multistep+1);
  for (auto & v : differS1) v.resize(multistep+1);
  
  for (int nth=0; nth<nthrds; nth++) {
    for (unsigned M=0; M<=multistep; M++) {
      differC1[nth][M].resize(Mmax+1, nmax);
      differS1[nth][M].resize(Mmax+1, nmax);
      differC1[nth][M].setZero();
      differS1[nth][M].setZero();
    }
  }
    
  unsigned sz2 = (multistep+1)*(Mmax+1)*nmax;
  workC1.resize(sz2);
  workC .resize(sz2);
  workS1.resize(sz2);
  workS .resize(sz2);

  // Initialize accumulation values
  //
  cylmass = 0.0;
  cylmass_made = false;
  cylmass1 = std::vector<double>(nthrds);
}

void PolarBasis::setup(void)
{				// Call normalization and kernel
  for (int m=0; m<=Mmax; m++) {	// with current binding from derived class
    for (int n=0; n<nmax; n++) {
      normM (m, n) = norm(n, m);
      krnl  (m, n) = knl (n, m);
      sqnorm(m, n) = sqrt(normM(m, n));
    }
  }
}  


PolarBasis::~PolarBasis()
{
  if (pcavar) {
    pthread_mutex_destroy(&cc_lock);
  }

#if HAVE_LIBCUDA==1
  if (component->cudaDevice>=0) destroy_cuda();
#endif
}

void PolarBasis::initialize()
{
				// Do nothing
}

void PolarBasis::check_range()
{
				// Do nothing
}

void PolarBasis::get_acceleration_and_potential(Component* C)
{
  nvTracerPtr tPtr;
  if (cuda_prof)
    tPtr = std::make_shared<nvTracer>("PolarBasis::get_acceleration");

#ifdef DEBUG
  cout << "Process " << myid 
       << ": in PolarBasis::get_acceleration_and_potential" << endl;
#endif
				
  cC = C;			// "Register" component
  nbodies = cC->Number();	// And compute number of bodies

  //======================================
  // Determine potential and acceleration 
  //======================================

  MPL_start_timer();

  determine_acceleration_and_potential();

  MPL_stop_timer();

  // Clear external potential flag
  use_external = false;

  //======================================
  // Dump coefficients for debugging
  //======================================

#ifdef DEBUG
  if (myid==0) {
    if ( (multistep && mstep==0) || !multistep) {
      static int cnt = 0;
      ostringstream sout;
      sout << "PolarBasis.debug." << runtag << "." << cnt++;
      ofstream out(sout.str().c_str());
      if (out) dump_coefs_all(out);
    }
  }
#endif

}


void * PolarBasis::determine_coefficients_thread(void * arg)
{
  double r, r2, facL, fac1, fac2, phi, mass;
  double xx, yy, zz;

  unsigned nbodies = component->levlist[mlevel].size();
  int id = *((int*)arg);
  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;
  double adb = component->Adiabatic();
  std::vector<double> wk(nmax);

#ifdef DEBUG
  pthread_mutex_lock(&io_lock);
  cout << "Process " << myid 
       << ", " << id
       << ": in determine_coefficients_thread"
       << ", rmax=" << rmax << endl;
  pthread_mutex_unlock(&io_lock);
#endif

  thread_timing_beg(id);

  vector<double> ctr;
  if (mix) mix->getCenter(ctr);

				// Compute potential using a 
				// subset of particles
  if (subset) nend = (int)floor(ssfrac*nend);

  unsigned whch = 0;		// For PCA jacknife

  for (int i=nbeg; i<nend; i++) {

    int indx = component->levlist[mlevel][i];

    if (component->freeze(indx)) continue;
    
    
    mass = component->Mass(indx) * adb;
				// Adjust mass for subset
    if (subset) mass /= ssfrac;
    
    if (mix) {
      xx = component->Pos(indx, 0, Component::Local) - ctr[0];
      yy = component->Pos(indx, 1, Component::Local) - ctr[1];
      zz = component->Pos(indx, 2, Component::Local) - ctr[2];
    } else {
      xx = component->Pos(indx, 0, Component::Local | Component::Centered);
      yy = component->Pos(indx, 1, Component::Local | Component::Centered);
      zz = component->Pos(indx, 2, Component::Local | Component::Centered);
    }

    r2 = (xx*xx + yy*yy);
    r = sqrt(r2) + DSMALL;
      
    if (r>=rmin and r<=rmax) {

      use[id]++;
      phi = atan2(yy,xx);
	
      sinecosine_R(Mmax, phi, cosm[id], sinm[id]);

      get_potl(r, zz, potd[id], id);
      
      if (compute) {
	muse1[id] += mass;
	if (pcavar) {
	  whch = indx % sampT;
	  pthread_mutex_lock(&cc_lock);
	  massT1[whch][0] += mass;
	  pthread_mutex_unlock(&cc_lock);
	}
      }

      //		m loop
      for (int m=0, moffset=0; m<=Mmax; m++) {

	if (m==0) {
	  for (int n=0; n<nmax; n++) {
	    wk[n] = potd[id](m, n)*mass/normM(m, m);
	    (*expcoef0[id][moffset])[n] += wk[n];
	  }

	  if (compute and pcavar) {
	    pthread_mutex_lock(&cc_lock);
	    for (int n=0; n<nmax; n++) {
	      (*expcoefT1[whch][m])[n] += wk[n];
	      for (int o=0; o<nmax; o++)
		(*expcoefM1[whch][m])(n, o) += wk[n]*wk[o]/mass;
	    }
	    pthread_mutex_unlock(&cc_lock);
	  }

	  if (compute) {
	    pthread_mutex_lock(&cc_lock);
	    for (int n=0; n<nmax; n++) {
	      for (int o=0; o<nmax; o++) {
		tvar[0][m](n, o) += wk[n]*wk[o]/mass;
	      }
	    }
	    pthread_mutex_unlock(&cc_lock);
	  }

	  moffset++;
	}
	else {
	  if (not M0_only) {

	    fac1 = cosm[id][m] * M_SQRT2;
	    fac2 = sinm[id][m] * M_SQRT2;

	    for (int n=0; n<nmax; n++) {

	      wk[n] = potd[id](m, n)*mass/normM(m, n);

	      (*expcoef0[id][moffset  ])[n] += wk[n]*fac1;
	      (*expcoef0[id][moffset+1])[n] += wk[n]*fac2;
	    }

	    if (compute and pcavar) {
	      pthread_mutex_lock(&cc_lock);
	      for (int n=0; n<nmax; n++) {
		(*expcoefT1[whch][m])[n] += wk[n]*facL;
		for (int o=0; o<nmax; o++)
		  (*expcoefM1[whch][m])(n, o) += wk[n]*wk[o]*facL*facL/mass;
	      }
	      pthread_mutex_unlock(&cc_lock);
	    }
	    
	    if (compute) {
	      pthread_mutex_lock(&cc_lock);
	      for (int n=0; n<nmax; n++) {
		for (int o=0; o<nmax; o++) {
		  tvar[m][0](n, o) += wk[n]*wk[o]/mass;
		}
	      }
	      pthread_mutex_unlock(&cc_lock);
	    }
	  }

	  moffset+=2;
	} // m!=0

      } // m loop

    } // r < rmax

  } // particle loop

  thread_timing_end(id);

  return (NULL);
}


void PolarBasis::determine_coefficients(void)
{
  if (play_back) {
    determine_coefficients_playback();
    if (play_cnew) determine_coefficients_particles();
  } else {
    determine_coefficients_particles();
  }
}

void PolarBasis::determine_coefficients_playback(void)
{
  // Do we need new coefficients?
  if (tnow <= lastPlayTime) return;
  lastPlayTime = tnow;

  // Set coefficient matrix size (only do it once)
  if (expcoefP.size()==0) {
    expcoefP.resize(2*Mmax+1);
    for (auto & v : expcoefP) v = std::make_shared<Eigen::VectorXd>(nmax);
  }

  if (coefMaster) {

    if (myid==0) {
      auto ret = playback->interpolate(tnow);

      // Get the matrix
      auto mat = std::get<0>(ret);

      // Get the error signal
      if (not std::get<1>(ret)) stop_signal = 1;

      //            +---- Counter in complex array (cosing and sine
      //            |     components are the real and imag parts)
      //            v
      for (int m=0, M=0; m<=Mmax; m++) {
	*expcoefP[M] = mat.row(M).real();
	MPI_Bcast((*expcoefP[M++]).data(), nmax, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	if (m) {
	  *expcoefP[M] = mat.row(M).imag();
	  MPI_Bcast((*expcoefP[M++]).data(), nmax, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	}
      }
    } else {
      for (int m=0, M=0; m<=Mmax; m++) {
	MPI_Bcast((*expcoefP[M++]).data(), nmax, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	if (m) {
	  MPI_Bcast((*expcoefP[M++]).data(), nmax, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	}
      }
    }
    
  } else {

    auto ret = playback->interpolate(tnow);

    // Get the matrix
    auto mat = std::get<0>(ret);

    // Get the error signal
    if (not std::get<1>(ret)) stop_signal = 1;

    for (int m=0, M=0; m<=Mmax; m++) {
      *expcoefP[M++] = mat.row(M).real();
      if (m) {
	*expcoefP[M++] = mat.row(M).imag();
      }
    }
  }
}

void PolarBasis::determine_coefficients_particles(void)
{
  nvTracerPtr tPtr;
  if (cuda_prof)
    tPtr = std::make_shared<nvTracer>("PolarBasis::determine_coefficients");

  std::chrono::high_resolution_clock::time_point start0, start1, finish0, finish1;

  start0 = std::chrono::high_resolution_clock::now();

  // Return if we should leave the coefficients fixed
  //
  if (!self_consistent && !firstime_coef && !initializing) return;

  if (pcavar) {
    if (this_step >= npca0) 
      compute = (mstep == 0) && !( (this_step-npca0) % npca);
    else
      compute = false;
  }


  int loffset, moffset, use1;

  if (compute) {

    if (massT.size() == 0) {	// Allocate storage for subsampling
      if (defSampT) sampT = defSampT;
      else          sampT = floor(sqrt(component->CurTotal()));
      massT    .resize(sampT, 0);
      massT1   .resize(nthrds);
      for (auto & v : massT1) v.resize(sampT, 0);
      
      expcoefT .resize(sampT);
      for (auto & t : expcoefT ) {
	t.resize((Mmax+1)*(Mmax+2)/2);
	for (auto & v : t) v = std::make_shared<Eigen::VectorXd>(nmax);
      }
      
      expcoefT1.resize(sampT);
      for (auto & t : expcoefT1) {
	t.resize((Mmax+1)*(Mmax+2)/2);
	for (auto & v : t) v = std::make_shared<Eigen::VectorXd>(nmax);
      }

      expcoefM .resize(sampT);
      for (auto & t : expcoefM ) {
	t.resize((Mmax+1)*(Mmax+2)/2);
	for (auto & v : t) v = std::make_shared<Eigen::MatrixXd>(nmax, nmax);
      }
      
      expcoefM1.resize(sampT);
      for (auto & t : expcoefM1) {
	t.resize((Mmax+1)*(Mmax+2)/2);
	for (auto & v : t) v = std::make_shared<Eigen::MatrixXd>(nmax, nmax);
      }

    }

    // Zero arrays?
    //
    if (mlevel==0) {
      for (int n=0; n<nthrds; n++) muse1[n] = 0.0;
      muse0 = 0.0;
      
      for (int n=0; n<nthrds; n++) use[n] = 0.0;

      if (pcavar) {
	for (auto & t : expcoefT1) { for (auto & v : t) v->setZero(); }
	for (auto & t : expcoefM1) { for (auto & v : t) v->setZero(); }
	for (auto & v : massT1)    { for (auto & u : v) u = 0;        }
      }
    }
  }

#ifdef DEBUG
  cout << "Process " << myid << ": in <determine_coefficients>" << endl;
#endif

  // Swap interpolation arrays
  //
  auto p = expcoefL[mlevel];
  
  expcoefL[mlevel] = expcoefN[mlevel];
  expcoefN[mlevel] = p;
  
  // Clean arrays for current level
  //
  for (auto & v : expcoefN[mlevel]) v->setZero();
    
  for (auto & v : expcoef0) { for (auto & u : v) u->setZero(); }
    
  use1 = 0;
  if (multistep==0) used = 0;
    
#ifdef DEBUG
  cout << "Process " << myid 
       << ": in <determine_coefficients>, about to thread, lev=" 
       << mlevel << endl;
#endif

#ifdef LEVCHECK
  MPI_Barrier(MPI_COMM_WORLD);
  for (int n=0; n<numprocs; n++) {
    if (n==myid) {
      if (myid==0) cout << "-------------------------------" << endl
			<< "Level check in Polar Basis:" << endl 
			<< "-------------------------------" << endl;
      cout << setw(4) << myid << setw(4) << mlevel;
      if (component->levlist[mlevel].size())
	cout << setw(12) << component->levlist[mlevel].size()
	     << setw(12) << component->levlist[mlevel].front()
	     << setw(12) << component->levlist[mlevel].back() << endl;
      else
	cout << setw(12) << component->levlist[mlevel].size()
	     << setw(12) << (int)(-1)
	     << setw(12) << (int)(-1) << endl;
      
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  if (myid==0) cout << endl;
#endif
    
  std::fill(use.begin(), use.end(), 0);

#if HAVE_LIBCUDA==1
  if (component->cudaDevice>=0 and use_cuda) {
    if (cudaAccumOverride) {
      component->CudaToParticles();
      exp_thread_fork(true);
    } else {
      start1  = std::chrono::high_resolution_clock::now();
      determine_coefficients_cuda(compute);
      DtoH_coefs(mlevel);
      finish1 = std::chrono::high_resolution_clock::now();
    }
  } else {
    exp_thread_fork(true);
  }
#else
  exp_thread_fork(true);
#endif
  
 #ifdef DEBUG
  cout << "Process " << myid << ": in <determine_coefficients>, thread returned, lev=" << mlevel << endl;
#endif

  // Sum up the results from each thread
  //
  for (int i=0; i<nthrds; i++) use1 += use[i];
  for (int i=1; i<nthrds; i++) {
    for (int l=0; l<(Mmax+1)*(Mmax+1); l++) (*expcoef0[0][l]) += (*expcoef0[i][l]);
  }
  
  if (multistep==0 or tnow==resetT) {
    used += use1;
  }
  
  for (int m=0, moffset=0; m<=Mmax; m++) {

    if (m==0) {
	  
      if (multistep)
	MPI_Allreduce ( (*expcoef0[0][moffset]).data(),
			(*expcoefN[mlevel][moffset]).data(),
			nmax, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      else
	MPI_Allreduce ( (*expcoef0[0][moffset]).data(),
			(*expcoef[moffset]).data(),
			nmax, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      
      moffset++;
      
    } else {
	
      if (multistep) {
	MPI_Allreduce ( (*expcoef0[0][moffset]).data(),
			(*expcoefN[mlevel][moffset]).data(),
			nmax, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	  
	  MPI_Allreduce ( (*expcoef0[0][moffset+1]).data(),
			  (*expcoefN[mlevel][moffset+1]).data(),
			  nmax, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	} else {
	  MPI_Allreduce ( (*expcoef0[0][moffset]).data(),
			  (*expcoef[moffset]).data(),
			  nmax, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	  
	  MPI_Allreduce ( (*expcoef0[0][moffset+1]).data(),
			  (*expcoef[moffset+1]).data(),
			  nmax, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      }
      moffset+=2;
    }
  }
  
  //======================================
  // Last level?
  //======================================
  
  if (mlevel==multistep) {

    //======================================
    // Multistep update
    //======================================

    if (multistep) compute_multistep_coefficients();

    //======================================
    // PCA computation
    //======================================
    
    if (compute) {
      for (int i=0; i<nthrds; i++) muse0 += muse1[i];
      MPI_Allreduce ( &muse0, &muse,  1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      parallel_gather_coef2();
    }

    pca_hall(compute);
  }

  print_timings("PolarBasis: coefficient timings");

#if HAVE_LIBCUDA==1
  if (component->timers) {
    auto finish0 = std::chrono::high_resolution_clock::now();
  
    std::chrono::duration<double> duration0 = finish0 - start0;
    std::chrono::duration<double> duration1 = finish1 - start1;

    std::cout << std::string(60, '=') << std::endl;
    std::cout << "== Coefficient evaluation [PolarBasis] level="
	      << mlevel << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Time in CPU: " << duration0.count()-duration1.count() << std::endl;
    if (component->cudaDevice>=0 and use_cuda) {
      std::cout << "Time in GPU: " << duration1.count() << std::endl;
    }
    std::cout << std::string(60, '=') << std::endl;
  }
#endif

  //================================
  // Dump coefficients for debugging
  //================================

  //  +--- Deep debugging. Set to 'false' for production.
  //  |
  //  v
  if (false and myid==0 and mstep==0 and mlevel==multistep) {

    std::cout << std::string(60, '-') << std::endl
	      << "-- PolarBasis T=" << std::setw(16) << tnow << std::endl
	      << std::string(60, '-') << std::endl;

    for (int m=0, moffset=0; m<=Mmax; m++) {

      if (m==0) {
	  
	for (int n=0; n<nmax; n++) {
	  std::cout  << std::setw(4)  << m
		     << std::setw(4)  << n
		     << std::setw(18) << (*expcoef[moffset])[n]
		     << std::endl;
	}

	  moffset++;
	  
	} else {
	  
	  for (int n=0; n<nmax; n++) {
	    std::cout << std::setw(4)  << m
		      << std::setw(4)  << n
		      << std::setw(18) << (*expcoef[moffset  ])[n]
		      << std::setw(18) << (*expcoef[moffset+1])[n]
		      << std::endl;
	  }

	  moffset+=2;
      }
    }
    // END: m loop
    std::cout << std::string(60, '-') << std::endl;
  }

  firstime_coef = false;
}

void PolarBasis::multistep_reset()
{
  if (play_back and not play_cnew) return;

  used   = 0;
  resetT = tnow;
}


void PolarBasis::multistep_update_begin()
{
  if (play_back and not play_cnew) return;
				// Clear the update matricies
  for (int n=0; n<nthrds; n++) {
    for (int M=mfirst[mstep]; M<=multistep; M++) {
      for (int m=0; m<=2*Mmax; m++) {
	for (int ir=0; ir<nmax; ir++) {
	  differ1[n][M](m, ir) = 0.0;
	}
      }
    }
  }

}

void PolarBasis::multistep_update_finish()
{
  if (play_back and not play_cnew) return;

				// Combine the update matricies
				// from all nodes
  unsigned sz = (multistep - mfirst[mstep]+1)*(2*Mmax+1)*nmax;
  unsigned offset0, offset1;

				// Zero the buffer space
				//
  for (unsigned j=0; j<sz; j++) pack[j] = unpack[j] = 0.0;

  // Pack the difference matrices
  //
  for (int M=mfirst[mstep]; M<=multistep; M++) {
    offset0 = (M - mfirst[mstep])*(2*Mmax+1)*nmax;
    for (int m=0; m<=2*Mmax; m++) {
      offset1 = m*nmax;
      for (int n=0; n<nthrds; n++) 
	for (int ir=0; ir<nmax; ir++) 
	  pack[offset0+offset1+ir] += differ1[n][M](m, ir);
    }
  }

  MPI_Allreduce (&pack[0], &unpack[0], sz, 
		 MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  
  //  +--- Deep debugging
  //  |
  //  v
  if (false and myid==0) {
    std::string filename = runtag + ".differ_cyl2d_" + component->name;
    std::ofstream out(filename, ios::app);
    std::set<int> L = {0, 1, 2};
    if (out) {
      out << std::string(10+16*nmax, '-') << std::endl;
      out << "# T=" << tnow << " mstep=" << mstep << std::endl;
      for (int M=mfirst[mstep]; M<=multistep; M++) {
	offset0 = (M - mfirst[mstep])*(2*Mmax+1)*nmax;
	for (int m=0; m<=2*Mmax; m++) {
	  if (L.find(m)==L.end()) continue;
	  offset1 = m*nmax;
	  out << std::setw(5) << M << std::setw(5) << m;
	  for (int ir=0; ir<nmax; ir++)
	    out << std::setw(16) << unpack[offset0+offset1+ir];
	  out << std::endl;
	}
      }
      out << std::string(10+16*nmax, '-') << std::endl;
      for (int m=0; m<=2*Mmax; m++) {
	if (L.find(m)==L.end()) continue;
	out << std::setw(5) << " *** " << std::setw(5) << m;
	for (int ir=0; ir<nmax; ir++)
	  out << std::setw(16) << (*expcoef[m])[ir];
	out << std::endl;
      }
      out << std::string(10+16*nmax, '-') << std::endl;
      out << std::string(10+16*nmax, '-') << std::endl;
    } else {
      std::cout << "Error opening test file <" << filename << "> at T=" << tnow
		<< std::endl;
    }
  }
  // END: deep debug

  // Update the local coefficients
  //
  for (int M=mfirst[mstep]; M<=multistep; M++) {
    offset0 = (M - mfirst[mstep])*(Mmax+1)*(Mmax+1)*nmax;
    for (int m=0; m<=2*Mmax; m++) {
      offset1 = m*nmax;
      for (int ir=0; ir<nmax; ir++)
	(*expcoefN[M][m])[ir] += unpack[offset0+offset1+ir];
    }
  }

}

void PolarBasis::multistep_update(int from, int to, Component *c, int i, int id)
{
  if (play_back and not play_cnew) return;
  if (c->freeze(i)) return;

  double mass = c->Mass(i) * component->Adiabatic();

				// Adjust mass for subset
  if (subset) mass /= ssfrac;

  double xx = c->Pos(i, 0, Component::Local | Component::Centered);
  double yy = c->Pos(i, 1, Component::Local | Component::Centered);
  double zz = c->Pos(i, 2, Component::Local | Component::Centered);
  
  double r2 = (xx*xx + yy*yy);
  double  r = sqrt(r2) + DSMALL;
      
  if (r<rmax) {

    double phi   = atan2(yy,xx);
    double val, val1, val2, fac1, fac2;
    int moffset;

    get_potl(r, zz, potd[id], 0);

    //
    // m loop
    //
    for (int m=0, moffset=0; m<=Mmax; m++) {
      if (m==0) {
	for (int n=0; n<nmax; n++) {
	  val = potd[id](m, n)*mass/normM(m, n);
	  
	  differ1[id][from](moffset, n) -= val;
	  differ1[id][  to](moffset, n) += val;
	}
	moffset++;

      } else {
	fac1 = cosm[id][m] * M_SQRT2;
	fac2 = sinm[id][m] * M_SQRT2;

	for (int n=0; n<nmax; n++) {
	  val1 = potd[id](m, n)*fac1*mass/normM(m, n);
	  val2 = potd[id](m, n)*fac2*mass/normM(m, n);

	  differ1[id][from](moffset  , n) -= val1;
	  differ1[id][from](moffset+1, n) -= val2;
	  differ1[id][  to](moffset  , n) += val1;
	  differ1[id][  to](moffset+1, n) += val2;
	}
	moffset+=2;
      }
    }
  }
  
}


void PolarBasis::compute_multistep_coefficients()
{
  if (play_back and not play_cnew) return;

#ifdef TMP_DEBUG
  Eigen::MatrixXd tmpcoef(2*Mmax+1, nmax);
  for (int m=0; m<=2*Mmax; l++) {
    for (int j=0; j<nmax; j++) tmpcoef(l, j) = (*expcoef[m])[j];
  }
#endif

  // Clean coefficient matrix
  // 
  for (int m=0; m<=2*Mmax; m++) expcoef[m]->setZero();

  // For debugging only
  //
  double saveF = 0.0, saveG = 0.0;
  
  // Interpolate to get coefficients above
  // 
  for (int M=0; M<mfirst[mdrft]; M++) {
    
    double numer = static_cast<double>(mdrft            - dstepL[M][mdrft]);
    double denom = static_cast<double>(dstepN[M][mdrft] - dstepL[M][mdrft]);

    double b = numer/denom;	// Interpolation weights
    double a = 1.0 - b;

    for (int m=0; m<=2*Mmax; m++) {
      for (int n=0; n<nmax; n++) {
	(*expcoef[m])[n] += a*(*expcoefL[M][m])[n] + b*(*expcoefN[M][m])[n];
	if (m==0 and n==1) saveF += a*(*expcoefL[M][m])[n] + b*(*expcoefN[M][m])[n];
      }
    }
    
    //  +--- Deep debugging
    //  |
    //  v
    if (false and myid==0) {
      std::cout << std::left << std::fixed
		<< "CYL2d INTERP M=" << std::setw(2) << M
		<< " mstep=" << std::setw(3) << mstep
		<< " mdrft=" << std::setw(3) << mdrft
		<< " T=" << std::setw(16) << tnow
		<< " a=" << std::setw(16) << a
		<< " b=" << std::setw(16) << b
		<< " L=" << std::setw(16) << (*expcoefL[M][0])[1]
		<< " N=" << std::setw(16) << (*expcoefN[M][0])[1]
		<< " d=" << std::setw(16) << a*(*expcoefL[M][0])[1] + b*(*expcoefN[M][0])[1]
		<< " f=" << std::setw(16) << (*expcoef[0])[1]
		<< std::endl << std::right;
    }

    if (false and myid==0) {
      std::cout << "CYL2d interpolate:"
		<< " M="     << std::setw( 3) << M
		<< " mstep=" << std::setw( 3) << mstep 
		<< " mstep=" << std::setw( 3) << mdrft
		<< " minS="  << std::setw( 3) << dstepL[M][mdrft]
		<< " maxS="  << std::setw( 3) << dstepN[M][mdrft]
		<< " T="     << std::setw(12) << tnow
		<< " a="     << std::setw(12) << a 
		<< " b="     << std::setw(12) << b 
		<< " L01="   << std::setw(12) << (*expcoefL[M][0])[1]
		<< " N01="   << std::setw(12) << (*expcoefN[M][0])[1]
		<< " c01="   << std::setw(12) << (*expcoef[0])[1]
		<< std::endl;
    }

				// Sanity debug check
				// 
    if (a<0.0 && a>1.0) {
      cout << "Process " << myid << ": interpolation error in multistep [a]" 
	   << endl;
    }
    if (b<0.0 && b>1.0) {
      cout << "Process " << myid << ": interpolation error in multistep [b]" 
	   << endl;
    }
  }
				// Add coefficients at or below this level
				// 
  for (int M=mfirst[mdrft]; M<=multistep; M++) {

    //  +--- Deep debugging
    //  |
    //  v
    if (false and myid==0) {
      std::cout << std::left << std::fixed
		<< "CYL2d FULVAL M=" << std::setw(2) << M
		<< " mstep=" << std::setw(3) << mstep
		<< " mdrft=" << std::setw(3) << mdrft
		<< std::endl << std::right;
    }

    for (int m=0; m<=2*Mmax; m++) {
      for (int n=0; n<nmax; n++) {
	(*expcoef[m])[n] += (*expcoefN[M][m])[n];
	if (m==0 and n==1)saveG += (*expcoefN[M][m])[n];
      }
    }
  }

  //  +--- Deep debugging
  //  |
  //  v
  if (false and myid==0) {
    std::cout << std::left << std::fixed
	      << "CYL2d FULVAL mstep=" << std::setw(3) << mstep
	      << "  mdrft=" << std::setw(3) << mdrft
	      << " f=" << std::setw(16) << (*expcoef[0])[1]
	      << std::endl << std::right;
  }

  if (false and myid==0) {
    std::cout << "CYL2d interpolated value:"
	      << " mlev="  << std::setw( 4) << mlevel
	      << " mstep=" << std::setw( 4) << mstep
	      << " mdrft=" << std::setw( 4) << mdrft
	      << " T="     << std::setw( 8) << tnow
	      << " c01="   << std::setw(14) << (*expcoef[0])[1]
	      << " u01="   << std::setw(14) << saveF
	      << " v01="   << std::setw(14) << saveG
	      << std::endl;
  }

#ifdef TMP_DEBUG
  if (myid==0) {

    static bool first = true;
    double maxval=0.0, maxdif=0.0, maxreldif=0.0, val;
    int irmax=1, mmax=0, irmaxdif=1, mmaxdif=0, irmaxrel=1, mmaxrel=0;
    for (int ir=0; ir<nmax; ir++) {
      for (int m=0; m<=2*Mmax; m++) {
	val = (*expcoef[m])[ir] - tmpcoef(m, ir);

	if (fabs((*expcoef[m])[ir]) > maxval) {
	  maxval = (*expcoef[m])[ir];
	  irmax = ir;
	  mmax = m;
	}
	if (fabs(val) > maxdif) {
	  maxdif = val;
	  irmaxdif = ir;
	  mmaxdif = m;
	}

	if (fabs(val/(*expcoef[l])[ir]) > maxreldif) {
	  maxreldif = val/(*expcoef[l])[ir];
	  irmaxrel = ir;
	  mmaxrel = l;
	}
      }
    }

    std::ofstream out(runtag+".coefs.diag", std::ios::app);
    if (first) {
      out << std::left << std::setw(15) << "# Time"
	  << std::left << std::setw(10) << "| mstep"
	  << std::left << std::setw(10) << "| mdrft"
	  << std::left << std::setw(18) << "| max(val)"
	  << std::left << std::setw(8)  << "| l"
	  << std::left << std::setw(8)  << "| n"
	  << std::left << std::setw(18) << "| max(dif)"
	  << std::left << std::setw(8)  << "| l"
	  << std::left << std::setw(8)  << "| n"
	  << std::left << std::setw(18) << "| max(reldif)"
	  << std::left << std::setw(8)  << "| l"
	  << std::left << std::setw(8)  << "| n"
	  << std::endl;
      first = false;
    }
    out << std::left << std::setw(15) << tnow
	<< std::left << std::setw(10) << mstep
	<< std::left << std::setw(10) << mdrft
	<< std::left << std::setw(18) << maxval
	<< std::left << std::setw(8)  << mmax
	<< std::left << std::setw(8)  << irmax
	<< std::left << std::setw(18) << maxdif
	<< std::left << std::setw(8)  << mmaxdif
	<< std::left << std::setw(8)  << irmaxdif
	<< std::left << std::setw(18) << maxreldif
	<< std::left << std::setw(8)  << mmaxrel
	<< std::left << std::setw(8)  << irmaxrel
	<< std::endl;
  }
#endif

#ifdef MULTI_DEBUG
  if (myid==0) {

    std::set<int> L = {0, 1};
    std::set<int> N = {0, 1};

    static bool first = true;

    std::ofstream out(runtag+".multi_diag", std::ios::app);

    if (first) {
      out << "#" << std::setw(9) << std::right << "Time "
	  << std::setw(4) << "m" << std::setw(4) << "n";
      for (int M=0; M<=multistep; M++)  out << std::setw(14) << M << " ";
      out << std::setw(14) << "Sum" << std::endl;
      first = false;
    }

    for (int m : L) {

      for (int n : N) {

	out << std::setw(10) << std::fixed << tnow
	    << std::setw(4) << m << std::setw(4) << n;

	double sum = 0.0, val;
	for (int M=0; M<mfirst[mdrft]; M++) {
      
	  double numer = static_cast<double>(mdrft            - dstepL[M][mdrft]);
	  double denom = static_cast<double>(dstepN[M][mdrft] - dstepL[M][mdrft]);

	  double b = numer/denom;	// Interpolation weights
	  double a = 1.0 - b;
      
	  val = a*(*expcoefL[M][m])[n] + b*(*expcoefN[M][m])[n];
	  sum += val;
	  out << std::setw(14) << val;
	}
	
	for (int M=mfirst[mdrft]; M<=multistep; M++) {
	  val = (*expcoefN[M][m])[n];
	  sum += val;
	  out << std::setw(14) << val;
	}

	out << std::setw(14) << sum << std::endl; // Next line

      }
      // END: N loop
    }
    // END: L loop
  }
  // END: MULTI_DEBUG
#endif
}

void * PolarBasis::determine_acceleration_and_potential_thread(void * arg)
{
  int moffset, m, ioff, indx, nbeg, nend;
  unsigned nbodies;
  double r, r0=0.0, fac, phi;
  double potr, potz, potl, potp, p, pc, drc, drs, dzc, dzs, ps, dfacp, facdp;

  double pos[3];
  double xx, yy, zz, mfactor=1.0;

  vector<double> ctr;
  if (mix) mix->getCenter(ctr);
  
  int id = *((int*)arg);

  thread_timing_beg(id);

  // If we are multistepping, compute accel only at or above <mlevel>
  //
  for (int lev=mlevel; lev<=multistep; lev++) {

    nbodies = cC->levlist[lev].size();

    if (nbodies==0) continue;

    nbeg = nbodies*(id  )/nthrds;
    nend = nbodies*(id+1)/nthrds;

#ifdef DEBUG
    pthread_mutex_lock(&io_lock);
    std::cout << "Process " << myid << ": in thread"
	      << " id=" << id 
	      << " level=" << lev
	      << " nbeg=" << nbeg << " nend=" << nend << std::endl;
    pthread_mutex_unlock(&io_lock);
#endif

    for (int i=nbeg; i<nend; i++) {

      indx = cC->levlist[lev][i];

      if (cC->freeze(indx)) continue;

      if (mix) {
	if (use_external) {
	  cC->Pos(pos, indx, Component::Inertial);
	  component->ConvertPos(pos, Component::Local);
	} else
	  cC->Pos(pos, indx, Component::Local);

	mfactor = mix->Mixture(pos);
	xx = pos[0] - ctr[0];
	yy = pos[1] - ctr[1];
	zz = pos[2] - ctr[2];
      } else {
	if (use_external) {
	  cC->Pos(pos, indx, Component::Inertial);
	  component->ConvertPos(pos, Component::Local | Component::Centered);
	} else
	  cC->Pos(pos, indx, Component::Local | Component::Centered);
	
	xx = pos[0];
	yy = pos[1];
	zz = pos[2];
      }	

      fac = mfactor;

      r = sqrt(xx*xx + yy*yy) + DSMALL;
      phi = atan2(yy, xx);

      if (r>rmax) {
	ioff = 1;
	r0 = r;
	r = rmax;
      }
      else
	ioff = 0;


      potl = potr = potz = potp = 0.0;
      
      get_dpotl(r, zz, potd[id], dpotR[id], dpotZ[id], id);

      if (!NO_M0) {
	get_pot_coefs_safe(0, *expcoef[0], p, drc, dzc, potd[id], dpotR[id], dpotZ[id]);
	if (ioff) {
	  p = drc = dzc = 0;
	}
	potl = fac*p;
	potr = fac*drc;
	potz = fac*dzc;
      }
      
      //		m loop
      //		------
      for (m=1, moffset=1; m<=Mmax; m++) {

	// Suppress m=1 terms?
	//
	if (NO_M1 && m==1) continue;
	
	// Suppress odd m terms?
	//
	if (EVEN_M && (m/2)*2 != m) continue;

				// Suppress all asymmetric terms
	if (M0_only and m!=0) continue;

	if (m==0) {
	  get_pot_coefs_safe(m, *expcoef[moffset], p, drc, dzc, potd[id], dpotR[id], dpotZ[id]);
	  if (ioff) {
	    p = drc = dzc = 0.0;
	  }
	  potl += fac*p;
	  potr += fac*drc;
	  potz += fac*dzc;
	  moffset++;
	}
	else {
	  get_pot_coefs_safe(m, *expcoef[moffset  ], pc, drc, dzc, potd[id], dpotR[id], dpotZ[id]);
	  get_pot_coefs_safe(m, *expcoef[moffset+1], ps, drs, dzs, potd[id], dpotR[id], dpotZ[id]);
	  if (ioff) {
	    pc = ps = drc = drs = dzc = dzs = 0.0;
	  }

	  potl += fac*(pc*  cosm[id][m] + ps*  sinm[id][m]) * M_SQRT2;
	  potr += fac*(drc* cosm[id][m] + drs* sinm[id][m]) * M_SQRT2;
	  potp += fac*(-pc* sinm[id][m] + ps*  cosm[id][m]) * M_SQRT2 * m;
	  potz += fac*(dzc* cosm[id][m] + dzs* sinm[id][m]) * M_SQRT2;
	  moffset +=2;
	}
      }

      fac = xx*xx + yy*yy;

      cC->AddAcc(indx, 0, -potr*xx/r);
      cC->AddAcc(indx, 1, -potr*yy/r);
      cC->AddAcc(indx, 2, -potz     );
      if (fac > DSMALL) {
	cC->AddAcc(indx, 0,  potp*yy/fac );
	cC->AddAcc(indx, 1, -potp*xx/fac );
	cC->AddAcc(indx, 2, -potz        );
      }
      if (use_external)
	cC->AddPotExt(indx, potl);
      else
	cC->AddPot(indx, potl);
    }
  }

  thread_timing_end(id);

  return (NULL);
}


void PolarBasis::determine_acceleration_and_potential(void)
{
  nvTracerPtr tPtr;
  if (cuda_prof)
    tPtr = std::make_shared<nvTracer>("PolarBasis::determine_acceleration");

  std::chrono::high_resolution_clock::time_point start0, start1, finish0, finish1;
  start0 = std::chrono::high_resolution_clock::now();

#ifdef DEBUG
  cout << "Process " << myid << ": in determine_acceleration_and_potential\n";
#endif

  if (play_back) {
    swap_coefs(expcoefP, expcoef);
  }

  if (use_external == false) {

    if (multistep && (self_consistent || initializing)) {
      compute_multistep_coefficients();
    }

  }

#if HAVE_LIBCUDA==1
  if (use_cuda and cC->cudaDevice>=0 and cC->force->cudaAware()) {
    if (cudaAccelOverride) {
      cC->CudaToParticles();
      exp_thread_fork(false);
      cC->ParticlesToCuda();
    } else {
      start1 = std::chrono::high_resolution_clock::now();
      //
      // Copy coefficients from this component to device
      //
      HtoD_coefs();
      //
      // Do the force computation
      //
      determine_acceleration_cuda();
      
      finish1 = std::chrono::high_resolution_clock::now();
    }
  } else {

    exp_thread_fork(false);

  }
#else

  exp_thread_fork(false);

#endif

#ifdef DEBUG
  if (not use_cuda) {
    cout << "PolarBasis: process " << myid << " returned from fork" << endl;
    cout << "PolarBasis: process " << myid << " name=<" << cC->name << ">";

    if (cC->Particles().size()) {

      unsigned long imin = std::numeric_limits<unsigned long>::max();
      unsigned long imax = 0, kmin = kmin, kmax = 0;
      for (auto p : cC->Particles()) {
	imin = std::min<unsigned long>(imin, p.first);
	imax = std::max<unsigned long>(imax, p.first);
	kmin = std::min<unsigned long>(kmin, p.second->indx);
	kmax = std::max<unsigned long>(kmax, p.second->indx);
      }
      
      cout << " bodies ["
	   << kmin << ", " << kmax << "], ["
	   << imin << ", " << imax << "]"
	   << " #=" << cC->Particles().size() << endl;
      
    } else
      cout << " zero bodies!" << endl;
  }
#endif

  if (play_back) {
    swap_coefs(expcoef, expcoefP);
  }


  print_timings("PolarBasis: acceleration timings");

#if HAVE_LIBCUDA==1
  if (component->timers) {
    finish0 = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> duration0 = finish0 - start0;
    std::chrono::duration<double> duration1 = finish1 - start1;
  
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "== Force evaluation [PolarBasis::" << cC->name
	      << "] level=" << mlevel << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Time in CPU: " << duration0.count()-duration1.count() << std::endl;
    if (cC->cudaDevice>=0) {
      std::cout << "Time in GPU: " << duration1.count() << std::endl;
    }
    std::cout << std::string(60, '=') << std::endl;
  }
#endif
}


void PolarBasis::get_pot_coefs(int m, const Eigen::VectorXd& coef,
			       double& p, double& dpr, double& dpz)
{
  double pp, ppr, ppz;

  pp = ppr = ppz = 0.0;

  for (int i=0; i<nmax; i++) {
    pp  += potd [0](m, i) * coef[i];
    ppr += dpotR[0](m, i) * coef[i];
    ppz += dpotZ[0](m, i) * coef[i];
  }

  p   = -pp;
  dpr = -ppr;
  dpz = -ppz;
}


void PolarBasis::get_pot_coefs_safe(int m, const Eigen::VectorXd& coef, 
				    double& p, double& dpr, double& dpz,
				    Eigen::MatrixXd& potd1,
				    Eigen::MatrixXd& dptr1,
				    Eigen::MatrixXd& dptz1)
{
  double pp=0, ppr=0, ppz=0;

  for (int i=0; i<nmax; i++) {
    pp  += potd1(m, i) * coef[i];
    ppr += dptr1(m, i) * coef[i];
    ppz += dptz1(m, i) * coef[i];
  }

  p   = -pp;
  dpr = -ppr;
  dpz = -ppz;
}


void PolarBasis::get_dens_coefs(int m, Eigen::VectorXd& coef, double& p)
{
  double pp;

  pp = 0.0;

  for (int i=0; i<nmax; i++)
    pp  += dend(m, i) * coef[i];

  p = pp;
}
				// Dump coefficients to a file

void PolarBasis::dump_coefs(ostream& out)
{
  if (NewCoefs) {

    // This is a node of simple {key: value} pairs.  More general
    // content can be added as needed.
    //
    YAML::Node node;

    node["id"    ] = id;
    node["time"  ] = tnow;
    node["nmax"  ] = nmax;
    node["mmax"  ] = Mmax;
    node["normed"] = true;

    // Serialize the node
    //
    YAML::Emitter y; y << node;
    
    // Get the size of the string
    //
    unsigned int hsize = strlen(y.c_str());
    
    // Write magic #
    //
    out.write(reinterpret_cast<const char *>(&cmagic),   sizeof(unsigned int));

    // Write YAML string size
    //
    out.write(reinterpret_cast<const char *>(&hsize),    sizeof(unsigned int));
    
    // Write YAML string
    //
    out.write(reinterpret_cast<const char *>(y.c_str()), hsize);

    double z;

    for (int ir=0; ir<nmax; ir++) {
      for (int m=0, moffset=0; m<=2*Mmax; m++) {
	if (m==0) {
	  out.write((char *)&(z=(*expcoef[moffset+0])[ir]), sizeof(double));
	  moffset += 1;
	} else {
	  out.write((char *)&(z=(*expcoef[moffset+0])[ir]), sizeof(double));
	  out.write((char *)&(z=(*expcoef[moffset+1])[ir]), sizeof(double));
	  moffset += 2;
	}
      }
    }

  } else {

    std::ostringstream sout;
    sout << id;

    char buf[64];
    for (int i=0; i<64; i++) {
      if (i<sout.str().length())  buf[i] = sout.str().c_str()[i];
      else                        buf[i] = '\0';
    }

    out.write((char *)&buf, 64*sizeof(char));
    out.write((char *)&tnow, sizeof(double));
    out.write((char *)&nmax, sizeof(int));
    out.write((char *)&Mmax, sizeof(int));

    for (int ir=0; ir<nmax; ir++) {
      for (int m=0; m<=2*Mmax; m++)
	out.write((char *)&(*expcoef[m])[ir], sizeof(double));
    }
  }

}

// Dump coefficients to an HDF5 file

void PolarBasis::dump_coefs_h5(const std::string& file)
{
  // Add the current coefficients
  auto cur = std::make_shared<CoefClasses::CylStruct>();

  cur->time   = tnow;
  cur->geom   = geoname[geometry];
  cur->id     = id;
  cur->time   = tnow;
  cur->mmax   = Mmax;
  cur->nmax   = nmax;

  cur->coefs.resize(2*Mmax+1, nmax);

  for (int ir=0; ir<nmax; ir++) {
    for (int m=0, offset=0; m<=Mmax; m++) {
      if (m==0) {
	cur->coefs(m, ir) = {(*expcoef[offset])[ir], 0.0};
	offset += 1;
      } else {
	cur->coefs(m, ir) = {(*expcoef[offset])[ir], (*expcoef[offset+1])[ir]};
	offset += 2;
      }
    }
  }

  // Add center
  //
  cur->ctr = component->getCenter(Component::Local | Component::Centered);

  // Check if file exists
  //
  if (std::filesystem::exists(file + ".h5")) {
    cylCoefs.clear();
    cylCoefs.add(cur);
    cylCoefs.ExtendH5Coefs(file);
  }
  // Otherwise, extend the existing HDF5 file
  //
  else {
    // Copy the YAML config.  We only need this on the first call.
    std::ostringstream sout; sout << conf;
    size_t hsize = sout.str().size() + 1;
    cur->buf = std::shared_ptr<char[]>(new char [hsize]);
    sout.str().copy(cur->buf.get(), hsize); // Copy to CoefStruct buffer

    // Add the name attribute.  We only need this on the first call.
    cylCoefs.setName(component->name);

    // And the new coefficients and write the new HDF5
    cylCoefs.clear();
    cylCoefs.add(cur);
    cylCoefs.WriteH5Coefs(file);
  }
}


void PolarBasis::determine_fields_at_point
(double x, double y, double z, 
 double *tdens0, double *tpotl0, 
 double *tdens,  double *tpotl, 
 double *tpotX,  double *tpotY, double *tpotZ)
{
  double R2  = x*x + y*y;
  double R   = sqrt(R2) + DSMALL;
  double phi = atan2(y, x);
  
  *tdens0 = *tpotl0 = *tdens = *tdens0 = 0.0;
  *tpotX  = *tpotY  = *tpotZ = 0.0;

  bool ioff = false;
  if (R>rmax) return;

  double tpotR, tpotz, tpotp;

  determine_fields_at_point_cyl(R, z, phi,
				tdens0, tpotl0, tdens, tpotl,
				&tpotR, &tpotz, &tpotp);
  *tpotX = tpotR*x/R;
  *tpotY = tpotR*y/R;
      
  if (R > DSMALL) {
    *tpotX +=  tpotp*y/R;
    *tpotY += -tpotp*x/R;
  }
}


void PolarBasis::determine_fields_at_point_cyl
(double R, double z, double phi, 
 double *tdens0, double *tpotl0, 
 double *tdens, double *tpotl, 
 double *tpotR, double *tpotz, double *tpotp)
{
  *tdens0 = *tpotl0 = *tdens = *tpotl = 0.0;
  *tpotR  = *tpotz  = *tpotp = 0.0;

  bool ioff = false;
  if (R>rmax) return;

  double p, dp, pc, ps, drc, drs, dzc, dzs;

  sinecosine_R(Mmax, phi, cosm[0], sinm[0]);

  get_dens (R, z, dend, 0);
  get_dpotl(R, z, potd[0], dpotR[0], dpotZ[0], 0);

  // m loop
  for (int m=0, moffset=0; m<=Mmax; m++) {
    if (m==0) {
      get_dens_coefs(m, *expcoef[moffset], p);
      *tdens0 = p;
      get_pot_coefs_safe(m, *expcoef[moffset], p, drc, dzc, potd[0], dpotR[0], dpotZ[0]);
      *tpotl = p;
      *tpotR = drc;
      *tpotz = dzc;

      moffset++;
    }
    else {
      get_dens_coefs(m, *expcoef[moffset  ], pc);
      get_dens_coefs(m, *expcoef[moffset+1], ps);
      *tdens += (pc*cosm[0][m] + ps*sinm[0][m]) * M_SQRT2;
      
      get_pot_coefs(m, *expcoef[moffset],   pc, drc, dzc);
      get_pot_coefs(m, *expcoef[moffset+1], ps, drs, dzs);

      *tpotl += (pc* cosm[0][m] + ps* sinm[0][m]) * M_SQRT2;
      *tpotR += (drc*cosm[0][m] + drs*sinm[0][m]) * M_SQRT2;
      *tpotz += (dzc*cosm[0][m] + dzs*sinm[0][m]) * M_SQRT2;
      *tpotp += (-pc*sinm[0][m] + ps* cosm[0][m]) * M_SQRT2;

      moffset +=2;
    }
  }

  *tpotl /= scale;
  *tpotR /= scale*scale;
  *tpotp /= scale;
}

void PolarBasis::determine_fields_at_point_sph
(double r, double theta, double phi, 
 double *tdens0, double *tpotl0, 
 double *tdens, double *tpotl, 
 double *tpotr, double *tpott, double *tpotp)
{
  *tdens0 = *tpotl0 = *tdens = *tpotl = 0.0;
  *tpotr  = *tpott  = *tpotp = 0.0;

  // Cylindrical coords
  double R = r, z = 0.0;

  bool ioff = false;
  if (R>rmax) return;

  determine_fields_at_point_cyl(R, z, phi,
				tdens0, tpotl0, tdens, tpotl,
				tpotr, tpott, tpotp);
}


void PolarBasis::dump_coefs_all(ostream& out)
{
  out << setw(10) << "Time:"   << setw(18) << tnow      << endl
      << setw(10) << "Nmax:"   << setw(8)  << nmax      << endl
      << setw(10) << "Mmax:"   << setw(8)  << Mmax      << endl
      << setw(10) << "Levels:" << setw(8)  << multistep << endl;
  
  out << setw(70) << setfill('=') << "=" << endl << setfill(' ');
  out << "Total" << endl;
  out << setw(70) << setfill('=') << "=" << endl << setfill(' ');

  for (int ir=0; ir<nmax; ir++) {
    for (int m=0; m<=2*Mmax; m++)
      out << setw(18) << (*expcoef[m])[ir];
    out << endl;
  }
  
  out << endl;

  if (multistep) {

    for (int M=0; M<=multistep; M++) {
      out << setw(70) << setfill('=') << "=" << endl << setfill(' ');
      out << "Level " << M << endl;
      out << setw(70) << setfill('=') << "=" << endl << setfill(' ');

      for (int ir=0; ir<nmax; ir++) {
	for (int m=0; m<=2*Mmax; m++)
	  out << setw(18) << (*expcoefN[M][m])[ir];
	out << endl;
      }
    }
  }

}

void PolarBasis::setup_accumulation(int mlevel)
{
  if (accum_cos.size()==0) {	// First time only

    accum_cos.resize(Mmax+1);
    accum_sin.resize(Mmax+1);

    cosL.resize(multistep+1);
    cosN.resize(multistep+1);
    sinL.resize(multistep+1);
    sinN.resize(multistep+1);

    howmany1.resize(multistep+1);
    howmany .resize(multistep+1, 0);

    for (unsigned M=0; M<=multistep; M++) {
      cosL[M] = std::make_shared<VectorD2>(nthrds);
      cosN[M] = std::make_shared<VectorD2>(nthrds);
      sinL[M] = std::make_shared<VectorD2>(nthrds);
      sinN[M] = std::make_shared<VectorD2>(nthrds);
      
      for (int nth=0; nth<nthrds; nth++) {
	cosL(M)[nth].resize(Mmax+1);
	cosN(M)[nth].resize(Mmax+1);
	sinL(M)[nth].resize(Mmax+1);
	sinN(M)[nth].resize(Mmax+1);
      }

      howmany1[M].resize(nthrds, 0);
    }

    for (unsigned M=0; M<=multistep; M++) {
      
      for (int nth=0; nth<nthrds; nth++) {
	
	for (int m=0; m<=Mmax; m++) {
	  
	  cosN(M)[nth][m].resize(nmax);
	  cosL(M)[nth][m].resize(nmax);
	  
	  if (m>0) {
	    sinN(M)[nth][m].resize(nmax);
	    sinL(M)[nth][m].resize(nmax);
	  }
	}
      }
    }
    
    for (int m=0; m<=Mmax; m++) {
      accum_cos[m].resize(nmax);
      if (m>0) accum_sin[m].resize(nmax);
    }
    
    for (unsigned M=0; M<=multistep; M++) {
      for (int nth=0; nth<nthrds; nth++) {
	for (int m=0; m<=Mmax; m++) {
	  cosN(M)[nth][m].setZero();
	  if (m>0) sinN(M)[nth][m].setZero();
	}
      }
    }

    if (pcavar and sampT>0) {
      for (int nth=0; nth<nthrds; nth++) {
	for (unsigned T=0; T<sampT; T++) {
	  numbT1[nth][T] = 0;
	  massT1[nth][T] = 0.0;
	  covV[nth][T].resize(Mmax+1);
	  covM[nth][T].resize(Mmax+1);
	  for (int mm=0; mm<=Mmax; mm++) {
	    covV[nth][T][mm].resize(nmax);
	    covM[nth][T][mm].resize(nmax, nmax);
	  }
	}
      }
    }
  }

  // Zero values on every pass
  //
  for (int m=0; m<=Mmax; m++) {
    accum_cos[m].setZero();
    if (m>0) accum_sin[m].setZero();
  }

  if ( pcavar and mlevel==0 and sampT>0) {

    for (int nth=0; nth<nthrds; nth++) {

      for (unsigned T=0; T<sampT; T++) {
	numbT1[nth][T] = 0;
	massT1[nth][T] = 0.0;
	for (int mm=0; mm<=Mmax; mm++) {
	  covV[nth][T][mm].setZero();
	  covM[nth][T][mm].setZero();
	}
      }
    }
  }

  // Reset particle counter
  //
  howmany[mlevel] = 0;

  // Swap buffers
  //
  auto p  = cosL[mlevel];
  cosL[mlevel] = cosN[mlevel];
  cosN[mlevel] = p;
    
  p       = sinL[mlevel];
  sinL[mlevel] = sinN[mlevel];
  sinN[mlevel] = p;
    
  // Clean current coefficient files
  //
  for (int nth=0; nth<nthrds; nth++) {
    
    howmany1[mlevel][nth] = 0;
      
    for (int m=0; m<=Mmax; m++) {
      cosN(mlevel)[nth][m].setZero();
      if (m>0) sinN(mlevel)[nth][m].setZero();
    }
  }
    
  coefs_made[mlevel] = false;

  // DONE
}

void PolarBasis::init_pca()
{
  if (pcavar) {
    if (defSampT) sampT = defSampT;
    else          sampT = floor(sqrt(cC->CurTotal()));

    pthread_mutex_init(&used_lock, NULL);

    covV  .resize(nthrds);
    covM  .resize(nthrds);
    numbT1.resize(nthrds);
    massT1.resize(nthrds);
    numbT .resize(sampT, 0);
    massT .resize(sampT, 0);

    for (int nth=0; nth<nthrds;nth++) {
      numbT1[nth].resize(sampT, 0);
      massT1[nth].resize(sampT, 0);

      covV[nth].resize(sampT);
      covM[nth].resize(sampT);
      for (unsigned T=0; T<sampT; T++) {
	covV[nth][T].resize(Mmax+1);
	covM[nth][T].resize(Mmax+1);
	for (int mm=0; mm<=Mmax; mm++) {
	  covV[nth][T][mm].resize(nmax);
	  covM[nth][T][mm].resize(nmax, nmax);
	}
      }
    }
  }
}


void PolarBasis::accumulate(double r, double z, double phi, double mass, 
			  unsigned long seq, int id, int mlevel, bool compute)
{

  if (coefs_made[mlevel]) {
    ostringstream ostr;
    ostr << "PolarBasis::accumulate: Process " << myid << ", Thread " << id 
	 << ": calling setup_accumulation from accumulate, aborting" << endl;
    throw GenericError(ostr.str(), __FILE__, __LINE__, 1039, false);
  }

  double rr = sqrt(r*r+z*z);
  if (rr/scale>getRtable()) return;

  howmany1[mlevel][id]++;

  double msin, mcos;
  int mm;
  
  double norm = -4.0*M_PI;
  
  unsigned whch;
  if (compute and pcavar) {
    whch = seq % sampT;
    pthread_mutex_lock(&used_lock);
    numbT1[id][whch] += 1;
    massT1[id][whch] += mass;
    pthread_mutex_unlock(&used_lock);
  }

  get_pot(vc[id], vs[id], r, z);

  for (mm=0; mm<=Mmax; mm++) {

    mcos = cos(phi*mm);
    msin = sin(phi*mm);

    for (int nn=0; nn<nmax; nn++) {
      double hold = norm * mass * mcos * vc[id](mm, nn);

      cosN(mlevel)[id][mm][nn] += hold;

      if (compute and pcavar) {
	double hc1 = vc[id](mm, nn)*mcos, hs1 = 0.0;
	if (mm) hs1 = vs[id](mm, nn)*msin;
	double modu1 = sqrt(hc1*hc1 + hs1*hs1);

	covV(id, whch, mm)[nn] += mass * modu1;

	for (int oo=0; oo<nmax; oo++) {
	  double hc2 = vc[id](mm, oo)*mcos, hs2 = 0.0;
	  if (mm) hs2 = vs[id](mm, oo)*msin;
	  double modu2 = sqrt(hc2*hc2 + hs2*hs2);

	  covM(id, whch, mm)(nn, oo) += mass * modu1 * modu2;
	}
      }

      if (mm>0) {
	hold = norm * mass * msin * vs[id](mm, nn);
	sinN(mlevel)[id][mm][nn] += hold;
      }
    }

    cylmass1[id] += mass;
  }

}

void PolarBasis::accumulate(std::vector<Particle>& part, int mlevel,
			  bool verbose, bool compute)
{
  double r, phi, z, mass;

  int ncnt=0;
  if (myid==0 && verbose) cout << endl;

  setup_accumulation(mlevel);

  for (auto p=part.begin(); p!=part.end(); p++) {

    double mass = p->mass;
    double r    = sqrt(p->pos[0]*p->pos[0] + p->pos[1]*p->pos[1]);
    double phi  = atan2(p->pos[1], p->pos[0]);
    double z    = p->pos[2];
    
    accumulate(r, z, phi, mass, p->indx, 0, mlevel, compute);

    if (myid==0 && verbose) {
      if ( (ncnt % 100) == 0) cout << "\r>> " << ncnt << " <<" << flush;
      ncnt++;
    }
  }

}
  

void PolarBasis::accumulate_thread(std::vector<Particle>& part, int mlevel, bool verbose)
{
  setup_accumulation(mlevel);

  std::vector<std::thread> t(nthrds);
 
  // Launch the threads
  //
  for (int id=0; id<nthrds; ++id) {
    t[id] = std::thread(&PolarBasis::accumulate_thread_call, this, id, &part, mlevel, verbose);
  }

  // Join the threads
  //
  for (int id=0; id<nthrds; ++id) {
    t[id].join();
  }
}


void PolarBasis::accumulate_thread_call(int id, std::vector<Particle>* p, int mlevel, bool verbose)
{
  int nbodies = p->size();
    
  if (nbodies == 0) return;

  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;

  int ncnt=0;
  if (myid==0 && id==0 && verbose) cout << endl;

  for (int n=nbeg; n<nend; n++) {
    
    double mass = (*p)[n].mass;
    double r    = sqrt((*p)[n].pos[0]*(*p)[n].pos[0] + (*p)[n].pos[1]*(*p)[n].pos[1]);
    double phi  = atan2((*p)[n].pos[1], (*p)[n].pos[0]);
    double z    = (*p)[n].pos[2];
    
    accumulate(r, z, phi, mass, (*p)[n].indx, id, mlevel);

    if (myid==0 && id==0 && verbose) {
      if ( (ncnt % 100) == 0) cout << "\r>> " << ncnt << " <<" << flush;
      ncnt++;
    }
  }

}
  

