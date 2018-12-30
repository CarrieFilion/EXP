#include "expand.h"

#include <stdlib.h>
#include <math.h>

#include <Cube.H>

Cube::Cube(const YAML::Node& conf) : PotAccel(conf)
{
  id = "Cube";

  nminx = nminy = nminz = 0;
  nmaxx = nmaxy = nmaxz = 16;

  initialize();

  imx = 1+2*nmaxx;
  imy = 1+2*nmaxy;
  imz = 1+2*nmaxz;
  jmax = imx*imy*imz;
  expccof = new KComplex* [nthrds];
  for (int i=0; i<nthrds; i++)
    expccof[i] = new KComplex[jmax];

  expreal = new double [jmax];
  expreal1 = new double [jmax];
  expimag = new double [jmax];
  expimag1 = new double [jmax];
  
  dfac = 2.0*M_PI;
  kfac = KComplex(0.0,dfac);
}

Cube::~Cube(void)
{
  for (int i=0; i<nthrds; i++) delete [] expccof[i];
  delete [] expccof;

  delete [] expreal;
  delete [] expreal1;
  delete [] expimag;
  delete [] expimag1;
}

void Cube::initialize(void)
{
  try {
    if (conf["nminx"]) nminx = conf["nminx"].as<int>();
    if (conf["nminy"]) nminy = conf["nminy"].as<int>();
    if (conf["nminz"]) nminz = conf["nminz"].as<int>();
    if (conf["nmaxx"]) nmaxx = conf["nmaxx"].as<int>();
    if (conf["nmaxy"]) nmaxy = conf["nmaxy"].as<int>();
    if (conf["nmaxz"]) nmaxz = conf["nmaxz"].as<int>();
  }
  catch (YAML::Exception & error) {
    if (myid==0) std::cout << "Error parsing parameters in Cube: "
			   << error.what() << std::endl
			   << std::string(60, '-') << std::endl
			   << "Config node"        << std::endl
			   << std::string(60, '-') << std::endl
			   << conf                 << std::endl
			   << std::string(60, '-') << std::endl;
    MPI_Finalize();
    exit(-1);
  }

}

void * Cube::determine_coefficients_thread(void * arg)
{

  int ix,iy,iz,indx;
  KComplex startx,starty,startz,facx,facy,facz;
  KComplex stepx,stepy,stepz;
  double mass;

  unsigned nbodies = cC->Number();
  int id = *((int*)arg);
  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;
  double adb = component->Adiabatic();

  use[id] = 0;

  PartMapItr it = cC->Particles().begin();
  unsigned long i;

  for (int q=0; q<nbeg; q++) it++;
  for (int q=nbeg; q<nend; q++) {

    i = it->first;
    it++;

    use[id]++;
    mass = cC->Mass(i) * adb;

				/* Truncate to cube with sides in [0,1] */
    
    if (cC->Pos(i, 0)<0.0)
      cC->AddPos(i, 0, (double)((int)fabs(cC->Pos(i, 0))) + 1.0);
    else
      cC->AddPos(i, 0, -(double)((int)cC->Pos(i, 0)));
    
    if (cC->Pos(i, 1)<0.0)
      cC->AddPos(i, 1, (double)((int)fabs(cC->Pos(i, 1))) + 1.0);
    else
      cC->AddPos(i, 1, -(double)((int)cC->Pos(i, 1)));
    
    if (cC->Pos(i, 2)<0.0)
      cC->AddPos(i, 2, (double)((int)fabs(cC->Pos(i, 2))) + 1.0);
    else
      cC->AddPos(i, 2, -(double)((int)cC->Pos(i, 2)));
    
    
				/* Recursion multipliers */
    stepx = exp(-kfac*cC->Pos(i, 0));
    stepy = exp(-kfac*cC->Pos(i, 1));
    stepz = exp(-kfac*cC->Pos(i, 2));
    
				/* Initial values */
    startx = exp(nmaxx*kfac*cC->Pos(i, 0));
    starty = exp(nmaxy*kfac*cC->Pos(i, 1));
    startz = exp(nmaxz*kfac*cC->Pos(i, 2));
    
    for (facx=startx, ix=0; ix<imx; ix++, facx*=stepx) {
      for (facy=starty, iy=0; iy<imy; iy++, facy*=stepy) {
	for (facz=startz, iz=0; iz<imz; iz++, facz*=stepz) {
	  
	  indx = imz*(iy + imy*ix) + iz;
	  expccof[id][indx] += mass*facx*facy*facz;
	  
	}
      }
    }
  }
    
  return (NULL);
}

void Cube::determine_coefficients(void)
{
				//  Coefficients are ordered as follows:
				//  n=-nmax,-nmax+1,...,0,...,nmax-1,nmax
				//  in a single array for each dimension
				//  with z dimension changing most rapidly
  // Clean 
  for (int indx=0; indx<jmax; indx++) {
    for (int i=0; i<nthrds; i++) expccof[i][indx] = 0.0;
    expreal[indx] = 0.0;
    expimag[indx] = 0.0;
  }

  exp_thread_fork(true);

  for (int i=0; i<nthrds; i++) use1 += use[i];
  
  MPI_Allreduce ( &use1, &use0,  1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  used = use0;

  for (int i=0; i<nthrds; i++) {
    for (int indx=0; indx<jmax; indx++) {
      expreal1[indx] = expccof[i][indx].real();
      expimag1[indx] = expccof[i][indx].imag();
    }
  }

  MPI_Allreduce( expreal1, expreal, jmax, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce( expimag1, expimag, jmax, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  for (int indx=0; indx<jmax; indx++)
    expccof[0][indx] = KComplex(expreal[indx], expimag[indx]);

}

void * Cube::determine_acceleration_and_potential_thread(void * arg)
{
  int ix,iy,iz,ii,jj,kk,indx;
  KComplex fac,startx,starty,startz,facx,facy,facz,dens,potl,accx,accy,accz;
  KComplex stepx,stepy,stepz;
  double k2;

  unsigned nbodies = cC->Number();
  int id = *((int*)arg);
  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;

  PartMapItr it = cC->Particles().begin();
  unsigned long i;

  for (int q=0; q<nbeg; q++) it++;
  for (int q=nbeg; q<nend; q++) {
    
    i = it->first; it++;

    accx = accy = accz = dens = potl = 0.0;
    
				/* Recursion multipliers */
    stepx = exp(kfac*cC->Pos(i, 0));
    stepy = exp(kfac*cC->Pos(i, 1));
    stepz = exp(kfac*cC->Pos(i, 2));
    
				/* Initial values (note sign change) */
    startx = exp(-nmaxx*kfac*cC->Pos(i, 0));
    starty = exp(-nmaxy*kfac*cC->Pos(i, 1));
    startz = exp(-nmaxz*kfac*cC->Pos(i, 2));
    
    for (facx=startx, ix=0; ix<imx; ix++, facx*=stepx) {
      for (facy=starty, iy=0; iy<imy; iy++, facy*=stepy) {
	for (facz=startz, iz=0; iz<imz; iz++, facz*=stepz) {
	  
	  indx = imz*(iy + imy*ix) + iz;
	  
	  fac = facx*facy*facz*expccof[0][indx];
	  dens += fac;
	  
				/* Compute wavenumber; recall that the */
				/* coefficients are stored as follows: */
				/* -nmax,-nmax+1,...,0,...,nmax-1,nmax */
	  ii = ix-nmaxx;
	  jj = iy-nmaxy;
	  kk = iz-nmaxz;
	  
				/* No contribution to acceleration and */
	                        /* potential ("swindle") for zero      */
	                        /* wavenumber */
	  
	  if (ii==0 && jj==0 && kk==0) continue;
	  
				/* Limit to minimum wave number */
	  
	  if (abs(ii)<nminx || abs(jj)<nminy || abs(kk)<nminz) continue;
	  
	  k2 = 4.0*M_PI/(dfac*dfac*(ii*ii + jj*jj + kk*kk));
	  potl -= k2*fac;
	  
	  accx -= k2*KComplex(0.0,-dfac*ii)*fac;
	  accy -= k2*KComplex(0.0,-dfac*jj)*fac;
	  accz -= k2*KComplex(0.0,-dfac*kk)*fac;
	  
	}
      }
    }
    
    cC->AddAcc(i, 0, Re(accx));
    cC->AddAcc(i, 1, Re(accy));
    cC->AddAcc(i, 2, Re(accz));
    if (use_external)
      cC->AddPotExt(i, Re(potl));
    else
      cC->AddPot(i, Re(potl));
  }
  
  return (NULL);
}

void Cube::get_acceleration_and_potential(Component* C)
{
  cC = C;

  exp_thread_fork(false);

  // Clear external potential flag
  use_external = false;
}
