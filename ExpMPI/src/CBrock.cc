#include "expand.h"
#include <CBrock.H>

CBrock::CBrock(string& line, MixtureBasis *m) : SphericalBasis(line, m)
{
  id = "Clutton-Brock sphere";

  initialize();

				// Initialize grids for potential
  uu.setsize(0, nmax-1);
  duu.setsize(0, nmax-1);

  setup();
}

double CBrock::knl(int n, int l)
{
  return 4.0*n*(n+2*l+2) + (2*l+1)*(2*l+3);
}

double CBrock::norm(int n, int l)
{
  return M_PI * knl(n,l) * 
    exp( 
	-log(2.0)*((double)(4*l+4))
	- lgamma((double)(1+n)) - 2.0*lgamma((double)(1+l) )
	+ lgamma((double)(2*l+n+2))
	)/(double)(l+n+1);
}


void CBrock::initialize(void)
{
}

void CBrock::get_dpotl(int lmax, int nmax, double r, Matrix& p, Matrix& dp,
		       int tid)
{
  double x, dx, fac, rfac, drfac, dfac1, dfac2;
  int l, n;

  x = r_to_rh(r);
  dx = d_r_to_rh(r);

  fac = 0.5*sqrt(1.0 - x*x);
  rfac = sqrt(0.5*(1.0 - x));
  drfac = -0.5/(1.0 - x*x);
  
  for (l=0; l<=lmax; l++) {
    dfac1 = 1.0 + x + 2.0*x*l;
    dfac2 = 2.0*(l + 1);

    get_ultra(nmax-1, (double)l,     x, u[tid]);
    get_ultra(nmax-1, (double)(l+1), x, du[tid]);

    for (n=0; n<nmax; n++) p[l][n+1] = rfac*u[tid][n];
    dp[l][1] = dx*drfac*dfac1*rfac*u[tid][0];
    for (n=1; n<nmax; n++) dp[l][n+1] = dx*rfac*(drfac*dfac1*u[tid][n] + 
						 dfac2*du[tid][n-1]);

    rfac *= fac;
  }

}

void CBrock::get_potl(int lmax, int nmax, double r, Matrix& p, int tid)
{
  double x, fac, rfac;
  int l, n;

  x = r_to_rh(r);
  fac = 0.5*sqrt(1.0 - x*x);
  rfac = sqrt(0.5*(1.0 - x));
  
  for (l=0; l<=lmax; l++) {
    get_ultra(nmax-1, (double)l, x, u[tid]);
    for (n=0; n<nmax; n++) p[l][n+1] = rfac*u[tid][n];
    rfac *= fac;
  }

}

void CBrock::get_dens(int lmax, int nmax, double r, Matrix& p, int tid)
{
  double x, fac, rfac;
  int l, n;

  x = r_to_rh(r);
  fac = 0.5*sqrt(1.0 - x*x);
  rfac = pow(0.5*(1.0 - x),2.5);

  for (l=0; l<=lmax; l++) {
    get_ultra(nmax-1, (double)l, x, u[tid]);
    for (n=0; n<nmax; n++) p[l][n+1] = krnl[l][n+1]*rfac*u[tid][n];
    rfac *= fac;
  }

}

void CBrock::get_potl_dens(int lmax, int nmax, double r, 
			   Matrix& p, Matrix& d, int tid)
{
  double x, fac, rfac_p, rfac_d;
  int l, n;

  x = r_to_rh(r);
  fac = 0.5*sqrt(1.0 - x*x);
  rfac_p = sqrt(0.5*(1.0 - x));
  rfac_d = pow(0.5*(1.0 - x),2.5);
  
  for (l=0; l<=lmax; l++) {
    get_ultra(nmax-1, (double)l, x, u[tid]);
    for (n=0; n<nmax; n++) {
      p[l][n+1] = rfac_p*u[tid][n];
      d[l][n+1] = krnl[l][n+1]*rfac_d*u[tid][n];
    }
    rfac_p *= fac;
    rfac_d *= fac;
  }
}


/*------------------------------------------------------------------------
 *                                                                       *
 *      Convert between reduced coordinate                               *
 *                                                                       *
 *                 r^2-1                                                 *
 *          rh =  -------                                                *
 *                 r^2+1                                                 *
 *                                                                       *
 *      and its inverse:                                                 *
 *                                                                       *
 *              (1+rh)^(1/2)                                             *
 *          r = ------------                                             *
 *              (1-rh)^(1/2)                                             *
 *                                                                       *
 *-----------------------------------------------------------------------*/


#define BIG 1.0e30
double CBrock::rh_to_r(double rh)
{
  if (rh>=1.0) 
    return BIG;
  else
    return sqrt( (1.0+rh)/(1.0-rh) );
}

double CBrock::d_r_to_rh(double r)
{
  double fac;

  fac = r*r + 1.0;;
  return 4.0*r/(fac*fac);
}

double CBrock::r_to_rh(double r)
{
  return (r*r-1.0)/(r*r+1.0);
}
#undef BIG
