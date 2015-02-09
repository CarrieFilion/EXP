#include "expand.h"

#include <interp.h>
#include <Bessel.H>

int Bessel::RNUM = 1000;

Bessel::Bessel(string& line, MixtureBasis* m) : SphericalBasis(line, m)
{
  id = "BesselForce";
  initialize();

  // Initialize radial grids

  make_grid(0, rmax, Lmax, nmax);

  setup();
}


Bessel::~Bessel(void)
{
  delete p;
}

void Bessel::initialize()
{
}

/*
double Bessel::norm(int l, int n)
{
  return 1.0;
}

double Bessel::knl(int l, int n)
{
  return 1.0;
}
*/
				/* Get potential functions by from table */

void Bessel::get_dpotl(int lmax, int nmax, double r, 
			   Matrix& p, Matrix& dp, int tid)
{
  double a,aa,aaa,b,bb,bbb;
  int klo, khi;
  int l, n;

  klo = (int)( (r-r_grid[1])/r_grid_del ) + 1;
  if (klo < 1) klo = 1;
  if (klo >= RNUM) klo = RNUM - 1;
  khi = klo + 1;

  a = (r_grid[khi] - r)/r_grid_del;
  b = (r - r_grid[klo])/r_grid_del;

  aa = a*(a*a-1.0)*r_grid_del*r_grid_del/6.0;
  bb = b*(b*b-1.0)*r_grid_del*r_grid_del/6.0;
  aaa = -(3.0*a*a - 1.0)*r_grid_del/6.0;
  bbb =  (3.0*b*b - 1.0)*r_grid_del/6.0;

  for (l=0; l<=lmax; l++) {
    for (n=1; n<=nmax; n++) {
      p[l][n] = a*potl_grid[l].rw[n][klo] + b*potl_grid[l].rw[n][khi] +
	aa*potl_grid[l].rw2[n][klo] + bb*potl_grid[l].rw2[n][khi];
      dp[l][n] = (-potl_grid[l].rw[n][klo]+potl_grid[l].rw[n][khi])/r_grid_del+
	aaa*potl_grid[l].rw2[n][klo] + bbb*potl_grid[l].rw2[n][khi];
    }
  }

}

void Bessel::get_potl(int lmax, int nmax, double r, Matrix& p, int tid)
{
  double a,aa,b,bb;
  int klo, khi;
  int l, n;

  klo = (int)( (r-r_grid[1])/r_grid_del ) + 1;
  if (klo < 1) klo = 1;
  if (klo >= RNUM) klo = RNUM - 1;
  khi = klo + 1;

  a = (r_grid[khi] - r)/r_grid_del;
  b = (r - r_grid[klo])/r_grid_del;

  aa = a*(a*a-1.0)*r_grid_del*r_grid_del/6.0;
  bb = b*(b*b-1.0)*r_grid_del*r_grid_del/6.0;

  for (l=0; l<=lmax; l++) {
    for (n=1; n<=nmax; n++) {
      p[l][n] = a*potl_grid[l].rw[n][klo] + b*potl_grid[l].rw[n][khi] +
	aa*potl_grid[l].rw2[n][klo] + bb*potl_grid[l].rw2[n][khi];
    }
  }
}

void Bessel::get_dens(int lmax, int nmax, double r, Matrix& p, int tid)
{
  double a,aa,b,bb;
  int klo, khi;
  int l, n;

  klo = (int)( (r-r_grid[1])/r_grid_del ) + 1;
  if (klo < 1) klo = 1;
  if (klo >= RNUM) klo = RNUM - 1;
  khi = klo + 1;

  a = (r_grid[khi] - r)/r_grid_del;
  b = (r - r_grid[klo])/r_grid_del;

  aa = a*(a*a-1.0)*r_grid_del*r_grid_del/6.0;
  bb = b*(b*b-1.0)*r_grid_del*r_grid_del/6.0;

  for (l=0; l<=lmax; l++) {
    for (n=1; n<=nmax; n++) {
      p[l][n] = a*dens_grid[l].rw[n][klo] + b*dens_grid[l].rw[n][khi] +
	aa*dens_grid[l].rw2[n][klo] + bb*dens_grid[l].rw2[n][khi];
    }
  }
}


void Bessel::get_potl_dens(int lmax, int nmax, double r, 
			       Matrix& p, Matrix& d, int tid)
{
  double a,aa,b,bb;
  int klo, khi;
  int l, n;

  klo = (int)( (r-r_grid[1])/r_grid_del ) + 1;
  if (klo < 1) klo = 1;
  if (klo >= RNUM) klo = RNUM - 1;
  khi = klo + 1;

  a = (r_grid[khi] - r)/r_grid_del;
  b = (r - r_grid[klo])/r_grid_del;

  aa = a*(a*a-1.0)*r_grid_del*r_grid_del/6.0;
  bb = b*(b*b-1.0)*r_grid_del*r_grid_del/6.0;

  for (l=0; l<=lmax; l++) {
    for (n=1; n<=nmax; n++) {
      p[l][n] = a*potl_grid[l].rw[n][klo] + b*potl_grid[l].rw[n][khi] +
	aa*potl_grid[l].rw2[n][klo] + bb*potl_grid[l].rw2[n][khi];
      d[l][n] = a*dens_grid[l].rw[n][klo] + b*dens_grid[l].rw[n][khi] +
	aa*dens_grid[l].rw2[n][klo] + bb*dens_grid[l].rw2[n][khi];
    }
  }
}

double Bessel::dens(double r, int n)
{
  double alpha;

  if (n>p->n)
    bomb("Routine dens() called with n out of bounds");

  alpha = p->a[n];
  return alpha*M_SQRT2/fabs(sbessj(p->l,alpha)) * pow(rmax,-2.5) *
    sbessj(p->l,alpha*r/rmax);
}

double Bessel::potl(double r, int n)
{
  double alpha;

  if (n>p->n)
    bomb("Routine potl() called with n out of bounds");

  alpha = p->a[n];
  return M_SQRT2/fabs(alpha*sbessj(p->l,alpha)) * pow(rmax,-0.5) *
    sbessj(p->l,alpha*r/rmax);
}

void Bessel::make_grid(double rmin, double rmax, int lmax, int nmax)
{
  
  potl_grid = vector<RGrid>(lmax+1);
  dens_grid = vector<RGrid>(lmax+1);
  
  r_grid.setsize(1, RNUM);

  r_grid_del = rmax/(double)(RNUM-1);
  double r = 0.0;
  for (int ir=1; ir<=RNUM; ir++, r+=r_grid_del)
    r_grid[ir] = r;

  for (int l=0; l<=lmax; l++) {
    potl_grid[l].rw.setsize(1,nmax,1,RNUM);
    potl_grid[l].rw2.setsize(1,nmax,1,RNUM);
    dens_grid[l].rw.setsize(1,nmax,1,RNUM);
    dens_grid[l].rw2.setsize(1,nmax,1,RNUM);

    p = new Roots(l, nmax);

    for (int n=1; n<=nmax; n++) {
      r = 0.0;
      for (int ir=1; ir<=RNUM; ir++, r+=r_grid_del) {
	potl_grid[l].rw[n][ir] = potl(r,n);
	dens_grid[l].rw[n][ir] = dens(r,n);
      }
      
      Spline(r_grid, potl_grid[l].rw[n], 1.0e30, 1.0e30, potl_grid[l].rw2[n]);
      Spline(r_grid, dens_grid[l].rw[n], 1.0e30, 1.0e30, dens_grid[l].rw2[n]);
    }

    delete p;

    potl_grid[l].nmax = nmax;
    dens_grid[l].nmax = nmax;
  }

  // check table

  for (int ir=1; ir<=RNUM; ir++) assert(!std::isnan(r_grid[ir]));
  for (int l=0; l<=lmax; l++) {
    assert(!std::isnan(potl_grid[l].nmax));
    assert(!std::isnan(dens_grid[l].nmax));
    for (int n=1; n<=nmax; n++) {
      for (int ir=1; ir<=RNUM; ir++) {
	assert(!std::isnan(potl_grid[l].rw[n][ir]));
	assert(!std::isnan(potl_grid[l].rw2[n][ir]));
	assert(!std::isnan(dens_grid[l].rw[n][ir]));
	assert(!std::isnan(dens_grid[l].rw2[n][ir]));
      }
    }
  }
  
}

