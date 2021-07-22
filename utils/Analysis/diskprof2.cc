/*****************************************************************************
 *  Description:
 *  -----------
 *
 *  Read in coefficients and compute VTK slices, and compute volume
 *  for VTK rendering
 *
 *
 *  Call sequence:
 *  -------------
 *
 *  Parameters:
 *  ----------
 *
 *
 *  Returns:
 *  -------
 *
 *
 *  Notes:
 *  -----
 *
 *
 *  By:
 *  --
 *
 *  MDW 11/28/08, 02/09/18
 *
 ***************************************************************************/

				// C++/STL headers
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>

				// BOOST stuff
#include <boost/shared_ptr.hpp>
#include <boost/make_unique.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp> 
#include <boost/random/mersenne_twister.hpp>


#include <yaml-cpp/yaml.h>	// YAML support

namespace po = boost::program_options;
namespace pt = boost::property_tree;

                                // System libs
#include <sys/time.h>
#include <sys/resource.h>

				// MDW classes
#include <numerical.H>
#include "Particle.h"
#include <PSP.H>
#include <interp.H>
#include <EmpCylSL.H>

#include <localmpi.H>
#include <foarray.H>

#include <VtkGrid.H>

#ifdef DEBUG
#ifndef _REDUCED
#pragma message "NOT using reduced particle structure"
#endif
#endif

const std::string overview = "Compute disk potential, force and density profiles from\nPSP phase-space output files";

				// Variables not used but needed for linking
int VERBOSE = 4;
int nthrds = 1;
int this_step = 0;
unsigned multistep = 0;
unsigned maxlev = 100;
int mstep = 1;
int Mstep = 1;
std::vector<int> stepL(1, 0), stepN(1, 1);
char threading_on = 0;
pthread_mutex_t mem_lock;
pthread_mutex_t coef_lock;
std::string outdir, runtag;
double tpos = 0.0;
double tnow = 0.0;
boost::mt19937 random_gen;
  
				// Globals
static  string outid;
static  double RMAX;
static  double ZMAX;
static  int OUTR;
static  int OUTZ;
  
static  bool AXIHGT;
static  bool VHEIGHT;
static  bool VOLUME;
static  bool SURFACE;
static  bool VSLICE;

// Center offset
//
std::vector<double> c0 = {0.0, 0.0, 0.0};

class Histogram
{
public:

  std::vector<double> dataXY;
  std::vector<std::vector<double>> dataZ;
  int N;
  double R, dR;
  
  Histogram(int N, double R) : N(N), R(R)
  {
    N = std::max<int>(N, 2);
    dR = 2.0*R/(N-1);		// Want grid points to be on bin centers
    dataXY.resize(N*N, 0.0);
    dataZ .resize(N*N);
  }

  void Reset() {
    std::fill(dataXY.begin(), dataXY.end(), 0.0);
    for (auto & v : dataZ) v.clear();
  }

  void Syncr() { 
    if (myid==0)
      MPI_Reduce(MPI_IN_PLACE, &dataXY[0], dataXY.size(), MPI_DOUBLE, MPI_SUM, 0,
		 MPI_COMM_WORLD);
    else
      MPI_Reduce(&dataXY[0],   &dataXY[0], dataXY.size(), MPI_DOUBLE, MPI_SUM, 0,
		 MPI_COMM_WORLD);

    std::vector<double> work;
    int nz;

    for (int n=1; n<numprocs; n++) {
      if (myid==0) {
	for (auto & v : dataZ) {
	  MPI_Recv(&nz, 1, MPI_INT, n, 110, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  work.resize(nz);
	  MPI_Recv(&work[0], nz, MPI_DOUBLE, n, 111, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  v.insert(std::end(v), std::begin(work), std::end(work));
	}
      } else if (myid==n) {
	for (auto v : dataZ) {
	  nz = v.size();
	  MPI_Send(&nz, 1,MPI_INT, 0, 110, MPI_COMM_WORLD);
	  MPI_Send(&v[0], nz, MPI_DOUBLE, 0, 111, MPI_COMM_WORLD);
	}
      }
    }

    if (myid==0) {
      for (auto & v : dataZ) std::sort(std::begin(v), std::end(v));
    }
  }

  void Add(double x, double y, double z, double m)
  {
    if (x < -R-0.5*dR or x >= R+0.5*dR or
	y < -R-0.5*dR or y >= R+0.5*dR  ) return;

    int indX = static_cast<int>(floor((x + R + 0.5*dR)/dR));
    int indY = static_cast<int>(floor((y + R + 0.5*dR)/dR));

    if (indX>=0 and indX<N and indY>=0 and indY<N) {
      dataXY[indX*N + indY] += m;
      dataZ [indX*N + indY].push_back(z);
    }
  }
};

void add_particles(PSPptr psp, int& nbods, vector<Particle>& p, Histogram& h)
{
  if (myid==0) {

    int nbody = nbods/numprocs;
    int nbody0 = nbods - nbody*(numprocs-1);

				// Send number of bodies to be received
				// by eacn non-root node
    MPI_Bcast(&nbody, 1, MPI_INT, 0, MPI_COMM_WORLD);

    vector<Particle>        t(nbody);
    vector<double>        val(nbody);
    vector<unsigned long> seq(nbody);

    SParticle *part = psp->GetParticle();
    Particle bod;
    
    //
    // Root's particles
    //
    for (int i=0; i<nbody0; i++) {
      if (part==0) {
	cerr << "Error reading particle [n=" << 0 << ", i=" << i << "]" << endl;
	exit(-1);
      }

      // Make a Particle
      //
      bod.mass = part->mass();
      for (int k=0; k<3; k++) bod.pos[k] = part->pos(k) - c0[k];
      for (int k=0; k<3; k++) bod.vel[k] = part->vel(k);
      bod.indx = part->indx();
      p.push_back(bod);

      part = psp->NextParticle();

      // Add to histogram
      //
      if (part) h.Add(part->pos(0) - c0[0],
		      part->pos(1) - c0[1],
		      part->pos(2) - c0[2],
		      part->mass());
    }

    //
    // Send the rest of the particles to the other nodes
    //
    for (int n=1; n<numprocs; n++) {
      
      for (int i=0; i<nbody; i++) {
	if (part==0) {
	  cerr << "Error reading particle [n=" 
	       << n << ", i=" << i << "]" << endl;
	  exit(-1);
	}
	t[i].mass = part->mass();
	for (int k=0; k<3; k++) t[i].pos[k] = part->pos(k) - c0[k];
	for (int k=0; k<3; k++) t[i].vel[k] = part->vel(k);
	part = psp->NextParticle();
      }
  
      for (int i=0; i<nbody; i++) val[i] = t[i].mass;
      MPI_Send(&val[0], nbody, MPI_DOUBLE, n, 11, MPI_COMM_WORLD);

      for (int i=0; i<nbody; i++) val[i] = t[i].pos[0];
      MPI_Send(&val[0], nbody, MPI_DOUBLE, n, 12, MPI_COMM_WORLD);

      for (int i=0; i<nbody; i++) val[i] = t[i].pos[1];
      MPI_Send(&val[0], nbody, MPI_DOUBLE, n, 13, MPI_COMM_WORLD);

      for (int i=0; i<nbody; i++) val[i] = t[i].pos[2];
      MPI_Send(&val[0], nbody, MPI_DOUBLE, n, 14, MPI_COMM_WORLD);

      for (int i=0; i<nbody; i++) val[i] = t[i].vel[0];
      MPI_Send(&val[0], nbody, MPI_DOUBLE, n, 15, MPI_COMM_WORLD);

      for (int i=0; i<nbody; i++) val[i] = t[i].vel[1];
      MPI_Send(&val[0], nbody, MPI_DOUBLE, n, 16, MPI_COMM_WORLD);

      for (int i=0; i<nbody; i++) val[i] = t[i].vel[2];
      MPI_Send(&val[0], nbody, MPI_DOUBLE, n, 17, MPI_COMM_WORLD);

      for (int i=0; i<nbody; i++) seq[i] = t[i].indx;
      MPI_Send(&seq[0], nbody, MPI_UNSIGNED_LONG, n, 18, MPI_COMM_WORLD);
    }

  } else {

    int nbody;
    MPI_Bcast(&nbody, 1, MPI_INT, 0, MPI_COMM_WORLD);
    vector<Particle>        t(nbody);
    vector<double>        val(nbody);
    vector<unsigned long> seq(nbody);
				// Get and pack

    MPI_Recv(&val[0], nbody, MPI_DOUBLE, 0, 11, 
	     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i=0; i<nbody; i++) t[i].mass = val[i];

    MPI_Recv(&val[0], nbody, MPI_DOUBLE, 0, 12, 
	     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i=0; i<nbody; i++) t[i].pos[0] = val[i];

    MPI_Recv(&val[0], nbody, MPI_DOUBLE, 0, 13, 
	     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i=0; i<nbody; i++) t[i].pos[1] = val[i];

    MPI_Recv(&val[0], nbody, MPI_DOUBLE, 0, 14, 
	     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i=0; i<nbody; i++) t[i].pos[2] = val[i];

    MPI_Recv(&val[0], nbody, MPI_DOUBLE, 0, 15, 
	     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i=0; i<nbody; i++) t[i].vel[0] = val[i];

    MPI_Recv(&val[0], nbody, MPI_DOUBLE, 0, 16, 
	     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i=0; i<nbody; i++) t[i].vel[1] = val[i];

    MPI_Recv(&val[0], nbody, MPI_DOUBLE, 0, 17, 
	     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i=0; i<nbody; i++) t[i].vel[2] = val[i];

    MPI_Recv(&seq[0], nbody, MPI_UNSIGNED_LONG, 0, 18, 
	     MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i=0; i<nbody; i++) t[i].indx = seq[i];

    p.insert(p.end(), t.begin(), t.end());

    // Add to histogram
    //
    for (auto part : t)
      h.Add(part.pos[0], part.pos[1], part.pos[2], part.mass);
  }

  // Synchronize histogram
  //
  h.Syncr();
}

void partition(PSPptr psp, std::string& name, vector<Particle>& p, Histogram& h)
{
  p.erase(p.begin(), p.end());

  PSPstanza* stanza;

  int iok = 1, nbods = 0;

  if (myid==0) {
    stanza = psp->GetNamed(name);
    if (stanza==0)
      iok = 0;
    else
      nbods = stanza->comp.nbod;
  }

  if (iok==0) {
    if (myid==0) std::cerr << "Could not find component named <" << name << ">" << std::endl;
    MPI_Finalize();
    exit(-1);
  }

  add_particles(psp, nbods, p, h);
}


				// Find peak density

double get_max_dens(Eigen::VectorXd& vv, double dz)
{
  int sz = vv.size();

  int ipeak=0;
  double pvalue=-1.0e18;

  for (int i=0; i<sz; i++) {
    if (vv[i] > pvalue) {
      ipeak = i;
      pvalue = vv[i];
    }
  }

  if (ipeak == 0)    ipeak = 1;
  if (ipeak == sz-1) ipeak = sz-2;

				// Solution of 2nd order Lagrange interpolation
				// formula
  double delta;
  double del  = vv[ipeak+1] - vv[ipeak-1];
  double ddel = vv[ipeak+1] + vv[ipeak-1] - 2.0*vv[ipeak];

  if (fabs(ddel) < 1.0e-4) 
    delta = 0.0;		// We're at the peak!
  else
    delta = - 0.5*del/ddel;

  return -ZMAX + dz*(ipeak + delta);

}



Eigen::VectorXd get_quart(Eigen::VectorXd& vv, double dz)
{
  int sz = vv.size();

  double next, prev=0.0;
  Eigen::VectorXd sum(sz), zz(sz);

  for (int i=0; i<sz; i++) {

    if (vv[i] > 0.0)
      next = vv[i];
    else
      next = 0.0;
    
    zz [i] = -ZMAX + dz*i;
    sum[i] = 0.5*(prev + next);	// Trapezoidal rule
    if (i>0) sum[i] += sum[i-1];
    prev = next;
  }

  double max = sum[sz-1];
  Eigen::VectorXd ret(3);
  
  ret[0] = odd2(0.25*max, sum, zz);
  ret[1] = odd2(0.50*max, sum, zz);
  ret[2] = odd2(0.75*max, sum, zz);

  return ret;
}

Eigen::VectorXd get_quart_truncated(Eigen::VectorXd& vv, double dz)
{
  int sz = vv.size();

  int ipeak=0;			// First find peak
  double pvalue=-1.0e18;

  for (int i=0; i<sz; i++) {
    if (vv[i] > pvalue) {
      ipeak = i;
      pvalue = vv[i];
    }
  }
				// Zero out above and below first zero
				// from peak

  int lo1 = ipeak;
  int hi1 = ipeak;

  for (; lo1>0; lo1--) {
    if (vv[lo1]<0.0) break;
  }

  for (; hi1<sz; hi1++) {
    if (vv[hi1]<0.0) break;
  }

  double next, prev=0.0;
  int sz1 = hi1 - lo1 + 1;
  Eigen::VectorXd sum(sz1), zz(sz1);

  for (int i=0; i<sz1; i++) {

    if (vv[i+lo1] > 0.0)
      next = vv[i+lo1];
    else
      next = 0.0;
    
    zz [i] = -ZMAX + dz*(i+lo1);
    sum[i] = 0.5*(prev + next);	// Trapezoidal rule
    if (i>0) sum[i] += sum[i-1];
    prev = next;
  }

  double max = sum[hi1];
  Eigen::VectorXd ret(3);
  
  ret[0] = odd2(0.25*max, sum, zz);
  ret[1] = odd2(0.50*max, sum, zz);
  ret[2] = odd2(0.75*max, sum, zz);

  return ret;
}


void write_output(EmpCylSL& ortho, int icnt, double time, Histogram& histo)
{
  unsigned ncnt = 0;
  int noutV = 9, noutS = 12;
  
  // ==================================================
  // Setup for output files
  // ==================================================
  
  ostringstream sstr;
  sstr << "." << std::setfill('0') << std::setw(5) << icnt;

  string suffix[12] = {"p0", "p1", "p", "fr", "fz", "fp", "d0", "d1", "d",
		       "z10", "z50", "z90"};

  // ==================================================
  // Axisymmetric structure (for GNUPLOT)
  // ==================================================

  if (AXIHGT) {
    
    double dR = RMAX/(OUTR-1);
    double dz = ZMAX/(OUTZ-1);
    double z, r, phi, hh, d0;
    Eigen::VectorXd vv(OUTZ);
    Eigen::VectorXd q;
    
    vector<double> indat(3*OUTR, 0.0), otdat(3*OUTR);
    
    for (int l=0; l<OUTR; l++) {
      
      if ( (ncnt++)%numprocs == myid ) {
	
	r = dR*l;
	
	for (int k=0; k<OUTZ; k++) {
	  z = -ZMAX + dz*k;
	  phi = 0.0;
	  vv[k] = ortho.accumulated_dens_eval(r, z, phi, d0);
	}
	
	indat[0*OUTR+l] = get_max_dens(vv, dz);
	// q = get_quart(vv, dz);
	q = get_quart_truncated(vv, dz);
	indat[1*OUTR+l] = q[0];
	indat[2*OUTR+l] = q[1] - q[-1];
      }
    }
    
    MPI_Reduce(&indat[0], &otdat[0], 3*OUTR, MPI_DOUBLE, MPI_SUM, 0, 
	       MPI_COMM_WORLD);
    
    if (myid==0) {

      std::string OUTF = outdir + "/" + runtag + "_" + outid + "_profile" + sstr.str();
      std::ofstream out(OUTF.c_str(), ios::out | ios::app);
      if (!out) {
	cerr << "Error opening <" << OUTF << "> for output\n";
	exit(-1);
      }
      out.setf(ios::scientific);
      out.precision(5);

      for (int l=0; l<OUTR; l++) {
	r = dR*l;
	out
	  << setw(15) << time
	  << setw(15) << r
	  << setw(15) << otdat[0*OUTR+l]
	  << setw(15) << otdat[1*OUTR+l]
	  << setw(15) << otdat[2*OUTR+l]
	  << endl;
      }
      out << endl;
    }
  }

  
  // ==================================================
  // Write vertical position of peak
  // ==================================================
  
  if (VHEIGHT) {
    
    double dR = 2.0*RMAX/(OUTR-1);
    double dz = 2.0*ZMAX/(OUTZ-1);
    double x, y, z, r, phi, hh, d0;
    Eigen::VectorXd vv(1, OUTZ);
    Eigen::VectorXd q;
    std::vector<double> indat(3*OUTR*OUTR, 0.0), otdat(3*OUTR*OUTR);
    
    for (int j=0; j<OUTR; j++) {
	
      x = -RMAX + dR*j;
	
      for (int l=0; l<OUTR; l++) {
      
	y = -RMAX + dR*l;
      
	if ( (ncnt++)%numprocs == myid ) {
	  
	  r = sqrt(x*x + y*y);
	  phi = atan2(y, x);
	  
	  for (int k=0; k<OUTZ; k++)
	    vv[k+1] = ortho.accumulated_dens_eval(r, -ZMAX + dz*k, phi, d0);
	  
	  // q = get_quart(vv, dz);
	  q = get_quart_truncated(vv, dz);
	  
	  indat[(0*OUTR + j)*OUTR + l] =  get_max_dens(vv, dz);
	  indat[(1*OUTR + j)*OUTR + l] =  q[1] - q[-1];
	  indat[(2*OUTR + j)*OUTR + l] =  q[0];
	}
      }
    }
      
      
    MPI_Reduce(&indat[0], &otdat[0], 3*OUTR*OUTR, MPI_DOUBLE, MPI_SUM, 0, 
	       MPI_COMM_WORLD);
      
    if (myid==0) {

      VtkGrid vtk(OUTR, OUTR, 1, -RMAX, RMAX, -RMAX, RMAX, 0, 0);

      std::string names[3] = {"surf", "height", "mid"};
      
      std::vector<double> data(OUTR*OUTR);

      for (int i=0; i<3; i++) {

	for (int l=0; l<OUTR; l++) {
	  for (int j=0; j<OUTR; j++) {
	    data[l*OUTR + j] = otdat[(i*OUTR + l)*OUTR + j];
	  }
	}

	vtk.Add(data, names[i]);
      }

      std::ostringstream sout;
      sout << outdir + "/" + runtag + "_" + outid + "_posn" + sstr.str();
      vtk.Write(sout.str());
    }
  }


  if (VOLUME) {
      
    // ==================================================
    // Write volume density
    // ==================================================
    
    double v;
    int valid = 1;
      
    double dR = 2.0*RMAX/(OUTR-1);
    double dz = 2.0*ZMAX/(OUTZ-1);
    double x, y, z, r, phi;
    double p0, d0, p1, fr, fz, fp;
    
    size_t blSiz = OUTZ*OUTR*OUTR;
    vector<double> indat(noutV*blSiz, 0.0), otdat(noutV*blSiz);
    
    for (int j=0; j<OUTR; j++) {
	  
      x = -RMAX + dR*j;
	  
      for (int l=0; l<OUTR; l++) {
	
	y = -RMAX + dR*l;
	
	for (int k=0; k<OUTZ; k++) {
      
	  z = -ZMAX + dz*k;

	  r = sqrt(x*x + y*y);
	  phi = atan2(y, x);
	  
	  ortho.accumulated_eval(r, z, phi, p0, p1, fr, fz, fp);
	  v = ortho.accumulated_dens_eval(r, z, phi, d0);
	  
	  size_t indx = (OUTR*j + l)*OUTR + k;

	  indat[0*blSiz + indx] = p0;
	  indat[1*blSiz + indx] = p1;
	  indat[2*blSiz + indx] = p0 + p1;
	  indat[3*blSiz + indx] = fr;
	  indat[4*blSiz + indx] = fz;
	  indat[5*blSiz + indx] = fp;
	  indat[6*blSiz + indx] = d0;
	  indat[7*blSiz + indx] = v;
	  indat[8*blSiz + indx] = d0 + v;
	}
      }
    }
    
    
    MPI_Reduce(&indat[0], &otdat[0], noutV*OUTZ*OUTR*OUTR, MPI_DOUBLE, MPI_SUM, 
	       0, MPI_COMM_WORLD);
    
    if (myid==0) {

      VtkGrid vtk(OUTR, OUTR, OUTZ, -RMAX, RMAX, -RMAX, RMAX, -ZMAX, ZMAX);

      std::vector<double> data(OUTR*OUTR*OUTZ);

      for (int n=0; n<noutV; n++) {

	for (int j=0; j<OUTR; j++) {
	  for (int l=0; l<OUTR; l++) {
	    for (int k=0; k<OUTZ; k++) {
	      size_t indx = (j*OUTR + l)*OUTR + k;

	      data[indx] = otdat[n*blSiz + indx];
	    }
	  }
	}
	vtk.Add(data, suffix[n]);
      }

      std::ostringstream sout;
      sout << outdir + "/" + runtag +"_" + outid + "_volume" + sstr.str();
      vtk.Write(sout.str());
    }

  }
  
  if (SURFACE) {
    
    // ==================================================
    // Write surface profile
    //   --- in plane ---
    // ==================================================
    
    double v;
    float f;
    
    double dR = 2.0*RMAX/(OUTR-1);
    double x, y, z=0.0, r, phi;
    double p0, d0, p1, fr, fz, fp;
    
    vector<double> indat(noutS*OUTR*OUTR, 0.0), otdat(noutS*OUTR*OUTR);
    
    for (int j=0; j<OUTR; j++) {
	
      x = -RMAX + dR*j;
      
      for (int l=0; l<OUTR; l++) {
      
	y = -RMAX + dR*l;
      
	if ((ncnt++)%numprocs == myid) {
	  
	  r = sqrt(x*x + y*y);
	  phi = atan2(y, x);
	  
	  ortho.accumulated_eval(r, z, phi, p0, p1, fr, fz, fp);
	  v = ortho.accumulated_dens_eval(r, z, phi, d0);
	  
	  indat[(0*OUTR+j)*OUTR+l] = p0;
	  indat[(1*OUTR+j)*OUTR+l] = p1;
	  indat[(2*OUTR+j)*OUTR+l] = p0 + p1;
	  indat[(3*OUTR+j)*OUTR+l] = fr;
	  indat[(4*OUTR+j)*OUTR+l] = fz;
	  indat[(5*OUTR+j)*OUTR+l] = fp;
	  indat[(6*OUTR+j)*OUTR+l] = d0;
	  indat[(7*OUTR+j)*OUTR+l] = v;
	  indat[(8*OUTR+j)*OUTR+l] = d0 + v;
	}
      }
    }
    
    MPI_Reduce(&indat[0], &otdat[0], noutS*OUTR*OUTR,
	       MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    
    
    if (myid==0) {
      
      for (int j=0; j<OUTR; j++) {
	for (int l=0; l<OUTR; l++) {
	  otdat[(9 *OUTR+j)*OUTR+l] = 0.0;
	  otdat[(10*OUTR+j)*OUTR+l] = 0.0;
	  otdat[(11*OUTR+j)*OUTR+l] = 0.0;
	  
	  // Check for number in the histogram bin
	  //
	  int numZ = histo.dataZ[j*OUTR+l].size();
	  if (numZ>0) {
	    otdat[(9 *OUTR+j)*OUTR+l] = histo.dataZ[j*OUTR+l][floor(0.1*numZ)];
	    otdat[(10*OUTR+j)*OUTR+l] = histo.dataZ[j*OUTR+l][floor(0.5*numZ)];
	    otdat[(11*OUTR+j)*OUTR+l] = histo.dataZ[j*OUTR+l][floor(0.9*numZ)];
	  }
	}
      }

      VtkGrid vtk(OUTR, OUTR, 1, -RMAX, RMAX, -RMAX, RMAX, 0, 0);

      std::vector<double> data(OUTR*OUTR);

      for (int n=0; n<noutS; n++) {
	for (int j=0; j<OUTR; j++) {
	  for (int l=0; l<OUTR; l++) {
	    data[j*OUTR + l] = otdat[(n*OUTR+j)*OUTR+l];
	  }
	}
	vtk.Add(data, suffix[n]);
      }

      vtk.Add(histo.dataXY, "histo");

      std::ostringstream sout;
      sout << outdir + "/" + runtag + "_" + outid + "_surface" + sstr.str();
      vtk.Write(sout.str());
    }
  } // END: SURFACE

  if (VSLICE) {
    
    // ==================================================
    // Write surface profile
    //   --- perp to plane ---
    // ==================================================
    
    double v;
    float f;
    
    double dR = 2.0*RMAX/(OUTR-1);
    double dZ = 2.0*ZMAX/(OUTZ-1);
    double x, y=0, z, r, phi;
    double p0, d0, p1, fr, fz, fp;
    
    std::vector<double> indat(noutV*OUTR*OUTR, 0.0), otdat(noutV*OUTR*OUTR);
    
      for (int j=0; j<OUTR; j++) {
	
	x = -RMAX + dR*j;

	for (int l=0; l<OUTZ; l++) {
      
	  z = -ZMAX + dZ*l;
      
	  if ((ncnt++)%numprocs == myid) {
	  
	  r   = sqrt(x*x + y*y);
	  phi = atan2(y, x);

	  ortho.accumulated_eval(r, z, phi, p0, p1, fr, fz, fp);
	  v = ortho.accumulated_dens_eval(r, z, phi, d0);
	  
	  indat[(0*OUTR+j)*OUTZ+l] = p0;
	  indat[(1*OUTR+j)*OUTZ+l] = p1;
	  indat[(2*OUTR+j)*OUTZ+l] = p0 + p1;
	  indat[(3*OUTR+j)*OUTZ+l] = fr;
	  indat[(4*OUTR+j)*OUTZ+l] = fz;
	  indat[(5*OUTR+j)*OUTZ+l] = fp;
	  indat[(6*OUTR+j)*OUTZ+l] = d0;
	  indat[(7*OUTR+j)*OUTZ+l] = v;
	  indat[(8*OUTR+j)*OUTZ+l] = d0 + v;
	}
      }
    }
    
    MPI_Reduce(&indat[0], &otdat[0], noutV*OUTR*OUTZ,
	       MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    
    
    if (myid==0) {
      
      VtkGrid vtk(OUTR, OUTZ, 1, -RMAX, RMAX, -ZMAX, ZMAX, 0, 0);

      std::vector<double> data(OUTR*OUTZ);

      for (int n=0; n<noutV; n++) {
	for (int j=0; j<OUTR; j++) {
	  for (int l=0; l<OUTZ; l++) {
	    data[j*OUTZ + l] = otdat[(n*OUTR+j)*OUTZ+l];
	  }
	}
	vtk.Add(data, suffix[n]);
      }

      std::ostringstream sout;
      sout << outdir + "/" + runtag + "_" + outid + "_vslice" + sstr.str();
      vtk.Write(sout.str());
    }
  } // END: VSLICE

}


void writePVD(const std::string& filename,
	      const std::vector<double>& times,
	      const std::vector<std::string>& files)
{
  // Sanity check
  //
  if (times.size() != files.size()) {
    std::cerr << "Mismatch in file and time arrays" << std::endl;
    exit(-3);
  }

  // Make file collection elements
  //
  pt::ptree ptC;

  for (size_t i=0; i<times.size(); i++) {
    boost::property_tree::ptree x;
    x.put("<xmlattr>.timestep", times[i]);
    x.put("<xmlattr>.part", 0);
    x.put("<xmlattr>.file", files[i]);

    ptC.add_child("DataSet", x);
  }

  // Add VTKFile attributes
  //
  pt::ptree ptP;
  
  ptP.put("<xmlattr>.type", "Collection");
  ptP.put("<xmlattr>.version", "0.1");
  ptP.put("<xmlattr>.byte_order", "LittleEndian");
  ptP.put("<xmlattr>.compressor", "vtkZLibDataCompressor");
  ptP.add_child("Collection", ptC);
  
  // Make the top-level property tree
  //
  pt::ptree PT;

  PT.add_child("VTKFile", ptP);

  // Write the property tree to the XML file.
  //
  pt::xml_parser::write_xml(filename.c_str(), PT, std::locale(), pt::xml_writer_make_settings<std::string>(' ', 4));

  std::cout << "Wrote PVD file <" << filename.c_str() << "> "
	    << " with " << times.size() << " data sets." << std::endl;
}


int
main(int argc, char **argv)
{
  int nice, numx, numy, lmax, mmax, nmax, norder;
  int initc, partc, beg, end, stride, init, cmapr, cmapz;
  double rcylmin, rcylmax, rscale, vscale, snr, Hexp=4.0;
  bool DENS, PCA, PVD, verbose = false, mask = false, ignore, logl;
  std::string CACHEFILE, COEFFILE, cname, dir("./");

  //
  // Parse Command line
  //
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h",
     "produce this help message")
    ("verbose,v",
     "verbose output")
    ("mask,b",
     "blank empty cells")
    ("OUT",
     "assume original, single binary PSP files as input")
    ("SPL",
     "assume new split binary PSP files as input")
    ("nice",
     po::value<int>(&nice)->default_value(0), 
     "number of bins in x direction")
    ("RMAX,R",
     po::value<double>(&RMAX)->default_value(0.1),
     "maximum radius for output")
    ("ZMAX,Z",
     po::value<double>(&ZMAX)->default_value(0.01),
     "maximum height for output")
    ("rcylmin",
     po::value<double>(&rcylmin)->default_value(0.001),
     "minimum radius for cylindrical basis table")
    ("rcylmax",
     po::value<double>(&rcylmax)->default_value(20.0),
     "maximum radius for cylindrical basis table")
    ("NUMX",
     po::value<int>(&numx)->default_value(128), 
     "number of radial table entries")
    ("NUMY",
     po::value<int>(&numy)->default_value(64), 
     "number of vertical table entries")
    ("rscale",
     po::value<double>(&rscale)->default_value(0.01), 
     "radial scale length for basis expansion")
    ("vscale",
     po::value<double>(&vscale)->default_value(0.001), 
     "vertical scale length for basis expansion")
    ("lmax",
     po::value<int>(&lmax)->default_value(36), 
     "maximum harmonic order for spherical expansion")
    ("nmax",
     po::value<int>(&nmax)->default_value(8),
     "maximum harmonic order for spherical expansion")
    ("mmax",
     po::value<int>(&mmax)->default_value(4), 
     "maximum azimuthal harmonic order for cylindrical expansion")
    ("norder",
     po::value<int>(&norder)->default_value(4), 
     "maximum radial order for each harmonic subspace")
    ("outr",
     po::value<int>(&OUTR)->default_value(40), 
     "number of radial points for output")
    ("outz",
     po::value<int>(&OUTZ)->default_value(40), 
     "number of vertical points for output")
    ("surface",
     po::value<bool>(&SURFACE)->default_value(true),
     "make equatorial slices")
    ("vslice",
     po::value<bool>(&VSLICE)->default_value(true),
     "make vertical slices")
    ("volume",
     po::value<bool>(&VOLUME)->default_value(false),
     "make volume for VTK rendering")
    ("axihgt",
     po::value<bool>(&AXIHGT)->default_value(false),
     "compute midplane height profiles")
    ("height",
     po::value<bool>(&VHEIGHT)->default_value(false),
     "compute height profiles")
    ("pca",
     po::value<bool>(&PCA)->default_value(false),
     "perform the PCA analysis for the disk")
    ("snr,S",
     po::value<double>(&snr)->default_value(-1.0),
     "if not negative: do a SNR cut on the PCA basis")
    ("center,C", po::value<std::vector<double> >(&c0)->multitoken(),
     "Accumulation center")
    ("diff",
     "render the difference between the trimmed and untrimmed basis")
    ("density",
     po::value<bool>(&DENS)->default_value(true),
     "compute density")
    ("pvd",
     po::value<bool>(&PVD)->default_value(false),
     "Compute PVD file for ParaView")
    ("compname",
     po::value<std::string>(&cname)->default_value("stars"),
     "train on Component (default=stars)")
    ("init",
     po::value<int>(&init)->default_value(0),
     "fiducial PSP index")
    ("beg",
     po::value<int>(&beg)->default_value(0),
     "initial PSP index")
    ("end",
     po::value<int>(&end)->default_value(99999),
     "final PSP index")
    ("stride",
     po::value<int>(&stride)->default_value(1),
     "PSP index stride")
    ("outdir",
     po::value<std::string>(&outdir)->default_value("."),
     "Output directory path")
    ("outfile",
     po::value<std::string>(&outid)->default_value("diskprof2"),
     "Filename prefix")
    ("cachefile",
     po::value<std::string>(&CACHEFILE)->default_value(".eof.cache.file"),
     "cachefile name")
    ("coeffile",
     po::value<std::string>(&COEFFILE),
     "coefficient output file name")
    ("cmapr",
     po::value<int>(&cmapr)->default_value(1),
     "Radial coordinate mapping type for cylindrical grid (0=none, 1=rational fct)")
    ("cmapz",
     po::value<int>(&cmapz)->default_value(1),
     "Vertical coordinate mapping type for cylindrical grid (0=none, 1=sech, 2=power in z")
    ("logl",
     po::value<bool>(&logl)->default_value(true),
     "use logarithmic radius scale in cylindrical grid computation")
    ("ignore",
     po::value<bool>(&ignore)->default_value(false),
     "rebuild EOF grid if input parameters do not match the cachefile")
    ("runtag",
     po::value<std::string>(&runtag)->default_value("run1"),
     "runtag for phase space files")
    ("dir,d",
     po::value<std::string>(&dir),
     "directory for SPL files")
    ;
  
  po::variables_map vm;

  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);    
  } catch (po::error& e) {
    std::cout << "Option error: " << e.what() << std::endl;
    exit(-1);
  }

  if (vm.count("help")) {
    std::cout << std::string(60, '-') << std::endl;
    std::cout << overview << std::endl;
    std::cout << std::string(60, '-') << std::endl << std::endl;
    std::cout << desc     << std::endl;
    return 1;
  }
 
  if (vm.count("verbose")) verbose = true;

  if (vm.count("mask")) mask = true;

  bool SPL = false;
  if (vm.count("SPL")) SPL = true;
  if (vm.count("OUT")) SPL = false;

#ifdef DEBUG
  sleep(20);
#endif  
  
  // ==================================================
  // MPI preliminaries
  // ==================================================

  local_init_mpi(argc, argv);
  
  // ==================================================
  // Nice process
  // ==================================================

  if (nice>0) setpriority(PRIO_PROCESS, 0, nice);

  // ==================================================
  // Parse center
  // ==================================================

  if (vm.count("center")) {
    if (c0.size() != 3) {
      if (myid==0) std::cout << "Center vector needs three components"
			     << std::endl;
      MPI_Finalize();
      exit(-1);
    }

    if (myid==0) {
      std::cout << "Using center: ";
      for (auto v : c0) std::cout << " " << v << " ";
      std::cout << std::endl;
    }
  }

  // ==================================================
  // PSP input stream
  // ==================================================

  int iok = 1;
  std::ostringstream s0, s1;
  if (myid==0) {
    if (SPL) s0 << "SPL.";
    else     s0 << "OUT.";
    s0 << runtag << "."
       << std::setw(5) << std::setfill('0') << init;

    std::string file = dir + s0.str();
    std::ifstream in(file);
    if (!in) {
      cerr << "Error opening <" << file << ">" << endl;
      iok = 0;
    }
  }
    
  MPI_Bcast(&iok, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (iok==0) {
    MPI_Finalize();
    exit(-1);
  }
    
  // ==================================================
  // All processes will now compute the basis functions
  // *****Using MPI****
  // ==================================================

  if (not ignore) {

    std::ifstream in(CACHEFILE);
    if (!in) {
      std::cerr << "Error opening cachefile named <" 
		<< CACHEFILE << "> . . ."
		<< std::endl
		<< "I will build <" << CACHEFILE
		<< "> but it will take some time."
		<< std::endl
		<< "If this is NOT what you want, "
		<< "stop this routine and specify the correct file."
		<< std::endl;
    } else {

      // Attempt to read magic number
      //
      unsigned int tmagic;
      in.read(reinterpret_cast<char*>(&tmagic), sizeof(unsigned int));

      //! Basis magic number
      const unsigned int hmagic = 0xc0a57a1;

      if (tmagic == hmagic) {
	// YAML size
	//
	unsigned ssize;
	in.read(reinterpret_cast<char*>(&ssize), sizeof(unsigned int));
	
	// Make and read char buffer
	//
	auto buf = boost::make_unique<char[]>(ssize+1);
	in.read(buf.get(), ssize);
	buf[ssize] = 0;		// Null terminate

	YAML::Node node;
      
	try {
	  node = YAML::Load(buf.get());
	}
	catch (YAML::Exception& error) {
	  if (myid)
	    std::cerr << "YAML: error parsing <" << buf.get() << "> "
		      << "in " << __FILE__ << ":" << __LINE__ << std::endl
		      << "YAML error: " << error.what() << std::endl;
	  throw error;
	}

	// Get parameters
	//
	mmax    = node["mmax"  ].as<int>();
	numx    = node["numx"  ].as<int>();
	numy    = node["numy"  ].as<int>();
	nmax    = node["nmax"  ].as<int>();
	norder  = node["norder"].as<int>();
	DENS    = node["dens"  ].as<bool>();
	if (node["cmap"])
	  cmapr = node["cmap"  ].as<int>();
	else
	  cmapr = node["cmapr" ].as<int>();
	if (node["cmapz"])
	  cmapz = node["cmapz"  ].as<int>();
	rcylmin = node["rmin"  ].as<double>();
	rcylmax = node["rmax"  ].as<double>();
	rscale  = node["ascl"  ].as<double>();
	vscale  = node["hscl"  ].as<double>();
	
      } else {
				// Rewind file
	in.clear();
	in.seekg(0);

	int tmp;
    
	in.read((char *)&mmax,    sizeof(int));
	in.read((char *)&numx,    sizeof(int));
	in.read((char *)&numy,    sizeof(int));
	in.read((char *)&nmax,    sizeof(int));
	in.read((char *)&norder,  sizeof(int));
	in.read((char *)&DENS,    sizeof(int)); 
	in.read((char *)&cmapr,   sizeof(int)); 
	in.read((char *)&rcylmin, sizeof(double));
	in.read((char *)&rcylmax, sizeof(double));
	in.read((char *)&rscale,  sizeof(double));
	in.read((char *)&vscale,  sizeof(double));
      }
    }
  }

  EmpCylSL::RMIN        = rcylmin;
  EmpCylSL::RMAX        = rcylmax;
  EmpCylSL::NUMX        = numx;
  EmpCylSL::NUMY        = numy;
  EmpCylSL::CMAPR       = cmapr;
  EmpCylSL::CMAPZ       = cmapz;
  EmpCylSL::logarithmic = logl;
  EmpCylSL::DENS        = DENS;
  EmpCylSL::CACHEFILE   = CACHEFILE;

				// Create expansion
				//
  EmpCylSL ortho(nmax, lmax, mmax, norder, rscale, vscale);
    
  vector<Particle> particles;
  PSPptr psp;
  Histogram histo(OUTR, RMAX);
  
  std::vector<double> times;
  std::vector<std::string> outfiles;

  bool compute = false;
  if (PCA) {
    EmpCylSL::PCAVAR = true;
    EmpCylSL::HEXP   = Hexp;
    if (vm.count("truncate"))
      ortho.setTK("Truncate");
    else
      ortho.setTK("Hall");
    
    compute = true;
    ortho.setup_accumulation();
  }

  // ==================================================
  // Initialize and/or create basis
  // ==================================================
  
  if (ortho.read_cache()==0) {
    
    if (myid==0) {
      if (SPL) psp = std::make_shared<PSPspl>(s0.str(), dir, true);
      else     psp = std::make_shared<PSPout>(s0.str(), true);
      std::cout << "Beginning disk partition [time="
		<< psp->CurrentTime()
		<< "] . . . " << std::flush;
    }
      
    partition(psp, cname, particles, histo);

    if (myid==0)
      std::cout << "done" << endl << endl
		<< setw(4) << "#" << "  " << setw(8) << "Number" << endl 
		<< setfill('-')
		<< setw(4) << "-" << "  " << setw(8) << "-" << endl
		<< setfill(' ');

    for (int n=0; n<numprocs; n++) {
      if (n==myid) {
	cout << setw(4) << myid << "  "
	     << setw(8) << particles.size() << endl;
      }
      MPI_Barrier(MPI_COMM_WORLD);
    }
    if (myid==0) cout << endl;

    //------------------------------------------------------------ 

    if (myid==0) cout << "Beginning disk accumulation . . . " << flush;
    ortho.setup_eof();
    ortho.setup_accumulation();
    ortho.accumulate_eof(particles, true);
    MPI_Barrier(MPI_COMM_WORLD);
    if (myid==0) cout << "done" << endl;
    
    //------------------------------------------------------------ 
    
    if (myid==0) cout << "Making the EOF . . . " << flush;
    ortho.make_eof();
    MPI_Barrier(MPI_COMM_WORLD);
    if (myid==0) cout << "done" << endl;
    
    //------------------------------------------------------------ 
    
    if (myid==0) cout << "Making disk coefficients . . . " << flush;
    ortho.make_coefficients(compute);
    MPI_Barrier(MPI_COMM_WORLD);
    if (myid==0) cout << "done" << endl;
    
    //------------------------------------------------------------ 
  }
  

  // ==================================================
  // Open output coefficient file
  // ==================================================
  
  std::ofstream coefs;
  if (myid==0 and COEFFILE.size()>0) {
    COEFFILE = outdir + "/" + COEFFILE;
    coefs.open(COEFFILE);
    if (not coefs) {
      std::cerr << "Could not open coefficient file <" << COEFFILE << "> . . . quitting"
		<< std::endl;
      MPI_Abort(MPI_COMM_WORLD, 10);
    }
  }

  std::string file;

  for (int indx=beg; indx<=end; indx+=stride) {

    // ==================================================
    // PSP input stream
    // ==================================================

    iok = 1;
    if (myid==0) {
      s1.str("");		// Clear stringstream
      if (SPL) s1 << "SPL.";
      else     s1 << "OUT.";
      s1 << runtag << "."<< std::setw(5) << std::setfill('0') << indx;
      
				// Check for existence of next file
      file = dir + s1.str();
      std::ifstream in(file);
      if (!in) {
	cerr << "Error opening <" << file << ">" << endl;
	iok = 0;
      }
    }
    
    MPI_Bcast(&iok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (iok==0) break;
    
    // ==================================================
    // Open frame list
    // ==================================================
    
    if (myid==0) {

      if (SPL) psp = std::make_shared<PSPspl>(s1.str(), dir, true);
      else     psp = std::make_shared<PSPout>(file, true);

      tnow = psp->CurrentTime();
      cout << "Beginning disk partition [time=" << tnow
	   << ", index=" << indx << "] . . . "  << flush;
      times.push_back(tnow);
      ostringstream filen;
      filen << runtag << "_" << outid << "_surface."
	    << std::setfill('0') << std::setw(5) << indx << ".vtr";
      outfiles.push_back(filen.str());
    }
      
    histo.Reset();		// Reset surface histogram

    partition(psp, cname, particles, histo);
    if (myid==0) cout << "done" << endl;
    
    ortho.setup_accumulation();
    if (PCA)
      ortho.setHall(outdir + "/" + runtag + "_" + outid + ".pca", particles.size());

    if (myid==0) cout << "Accumulating particle positions . . . " << flush;
    ortho.accumulate(particles, 0, true, compute);
    //                             ^     ^
    //                             |     |
    // Verbose --------------------+     |
    // Compute covariance ---------------+
    
    MPI_Barrier(MPI_COMM_WORLD);
    if (myid==0) cout << "done" << endl;
      
    //------------------------------------------------------------ 
      
    if (myid==0) cout << "Making disk coefficients . . . " << flush;
    ortho.make_coefficients(compute);
    MPI_Barrier(MPI_COMM_WORLD);
    if (myid==0) cout << "done" << endl;
      
    //------------------------------------------------------------ 

    if (myid==0) {
      std::cout << "Writing disk coefficients . . . " << flush;
      ortho.dump_coefs_binary(coefs, tnow);
      cout << "done" << endl;
    }
    MPI_Barrier(MPI_COMM_WORLD);

    //------------------------------------------------------------ 
      
    if (myid==0) cout << "Writing output . . . " << flush;
    //
    // Coefficient trimming
    //
    if (PCA and snr>=0.0) {
      std::vector<Eigen::VectorXd> ac_cos, ac_sin;
      std::vector<Eigen::VectorXd> rt_cos, rt_sin, sn_rat;
      ortho.pca_hall(true, false);
      ortho.get_trimmed(snr, ac_cos, ac_sin,
			&rt_cos, &rt_sin, &sn_rat);
      for (int mm=0; mm<=mmax; mm++) {
	if (vm.count("diff")) {
	  ac_cos[mm] = ac_cos[mm] - rt_cos[mm];
	  if (mm) 
	    ac_sin[mm] = ac_sin[mm] - rt_sin[mm];
	}
	if (mm==0)
	  ortho.set_coefs(mm, ac_cos[mm], ac_sin[mm], true);
	else
	  ortho.set_coefs(mm, ac_cos[mm], ac_sin[mm], false);
      }
      // END: M loop
    }

    double time = 0.0;
    if (myid==0) time = psp->CurrentTime();
    write_output(ortho, indx, time, histo);
    MPI_Barrier(MPI_COMM_WORLD);
    if (myid==0) cout << "done" << endl;
    
    //------------------------------------------------------------ 
    
  } // Dump loop

  // Create PVD file
  //
  if (myid==0 and PVD) {
    writePVD(outdir + "/" + runtag + ".pvd", times, outfiles);
  }

  // Shutdown MPI
  //
  MPI_Finalize();
    
  // DONE
  //
  return 0;
  }

