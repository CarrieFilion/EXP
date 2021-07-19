// This may look like C code, but it is really -*- C++ -*-

/*****************************************************************************
 *  Description:
 *  -----------
 *
 *  This routine computes 1d biorthgonal functions
 *
 *
 *  Call sequence:
 *  -------------
 *
 *  Parameters:
 *  ----------
 *
 *  x        as above
 *
 *  Returns:
 *  -------
 *
 *  Value
 *
 *  Notes:
 *  -----
 *
 *
 *  By:
 *  --
 *
 *  MDW 11/13/88
 *
 ***************************************************************************/

#include <math.h>
#include <numerical.H>
#include <biorth1d.H>

double OneDTrig::KSTOL=1.0e-6;
double OneDTrig::KSZTOL=1.0e-10;

//======================================================================
// Trig class implementation
//======================================================================

OneDTrig::OneDTrig(void) : OneDBiorth()
{
  kx = 0.0;
  zmax = 1.0;
  nrmax = -1;

  BiorthID = "OneDimensionalTrig";
}

OneDTrig::OneDTrig(double KX) : OneDBiorth()
{
  kx = KX;
  zmax = 1.0;
  nrmax = -1;

  compute_kstar(200);
  compute_norm();

  BiorthID = "OneDimensionalTrig";
}


OneDTrig::OneDTrig(double KX, double ZMAX) : OneDBiorth()
{
  kx = KX;
  zmax = ZMAX;
  nrmax = -1;

  compute_kstar(200);
  compute_norm();

  BiorthID = "OneDimensionalTrig";
}

void OneDTrig::reset(double KX, double ZMAX)
{
  kx = KX;
  zmax = ZMAX;
  nrmax = -1;

  compute_kstar(200);
  compute_norm();
}



void OneDTrig::compute_norm(void)
{
  cnorm.setsize(1, 2*nrmax+1);

  int n;

  for (int nn=1; nn<=nrmax; nn++) {
    
    n = nn-1;
    cnorm[1+2*n] = sqrt(1.0/(zmax * 
       (1.0 + 0.5*sin(2.0*kstar[n]*zmax)/(kstar[n]*zmax) ) ) );

    cnorm[2+2*n] = sqrt(1.0/(zmax * 
       (1.0 - 0.5*sin(2.0*kbstar[n]*zmax)/(kbstar[n]*zmax) ) ) );
  }

}

double OneDTrig::potl(int nn, int zilch, double z)
{
  int n = (nn-1)/2;
  double zz = fabs(z);

  if (zz > zmax) {

    if (2*(nn/2) != nn)
      return cnorm[nn]/sqrt(kstar[n]*kstar[n] + kx*kx) * cos(kstar[n]*zmax) 
	* exp(-kx*(zz-zmax));

    else
      return cnorm[nn]/sqrt(kbstar[n]*kbstar[n] + kx*kx) * sin(kbstar[n]*zmax)
	* exp(-kx*(zz-zmax));

  } else {

    if (2*(nn/2) != nn)
      return cnorm[nn]/sqrt(kstar[n]*kstar[n] + kx*kx) * cos(kstar[n]*z);
    else
      return cnorm[nn]/sqrt(kbstar[n]*kbstar[n] + kx*kx) * sin(kbstar[n]*z);

  }

}

double OneDTrig::dens(int nn, int zilch, double z)
{
  int n = (nn-1)/2;
  double zz = fabs(z);

  if (zz > zmax)  return 0.0;

  if (2*(nn/2) != nn)
    return cnorm[nn]*sqrt(kstar[n]*kstar[n] + kx*kx) * cos(kstar[n]*z);
  else
    return cnorm[nn]*sqrt(kbstar[n]*kbstar[n] + kx*kx) * sin(kbstar[n]*z);

}

double OneDTrig::force(int nn, int zilch, double z)
{
  int n = (nn-1)/2;
  double zz = fabs(z);

  if (zz > zmax) {

    if (2*(nn/2) != nn)
      return cnorm[nn]/sqrt(kstar[n]*kstar[n] + kx*kx) * cos(kstar[n]*zmax) 
	* exp(-kx*(zz-zmax)) * kx * z/(zz+1.0e-10);

    else
      return cnorm[nn]/sqrt(kbstar[n]*kbstar[n] + kx*kx) * sin(kbstar[n]*zmax)
	* exp(-kx*(zz-zmax)) * kx * z/(zz+1.0e-10);

  } else {

    if (2*(nn/2) != nn)
      return cnorm[nn]/sqrt(kstar[n]*kstar[n] + kx*kx) * sin(kstar[n]*z)
	* kstar[n];
    else
      return -cnorm[nn]/sqrt(kbstar[n]*kbstar[n] + kx*kx) * cos(kbstar[n]*z)
	* kbstar[n];

  }

}

void OneDTrig::potl(int zilch1, int zilch2, double z, Vector& ret)
{
  int nnmax = ret.gethigh();
  int n=0;
  double zz = fabs(z);

  for (int nn=1; nn<=nnmax; nn++) {

    if (zz > zmax)
      ret[nn] = cnorm[nn]/sqrt(kstar[n]*kstar[n] + kx*kx) * cos(kstar[n]*zmax)
	* exp(-kx*(zz-zmax));
    else
      ret[nn] = cnorm[nn]/sqrt(kstar[n]*kstar[n] + kx*kx) * cos(kstar[n]*z);

    if (nn++ == nnmax) return;

    if (zz > zmax)
      ret[nn] = cnorm[nn]/sqrt(kbstar[n]*kbstar[n] + kx*kx) * sin(kbstar[n]*zmax)
	* exp(-kx*(zz-zmax));
    else
      ret[nn] = cnorm[nn]/sqrt(kbstar[n]*kbstar[n] + kx*kx) * sin(kbstar[n]*z);
    
    n++;
  }

}

void OneDTrig::dens(int zilch1, int zilch2, double z, Vector& ret)
{
  int nnmax = ret.gethigh();
  int n=0;

  if (fabs(z) > zmax) {
    ret.zero();
    return;
  }

  for (int nn=1; nn<=nnmax; nn++) {

    ret[nn] = cnorm[nn]*sqrt(kstar[n]*kstar[n] + kx*kx) * cos(kstar[n]*z);
    if (nn++ == nnmax) return;

    ret[nn] = cnorm[nn]*sqrt(kbstar[n]*kbstar[n] + kx*kx) * sin(kbstar[n]*z);

    n++;
  }

}


void OneDTrig::force(int zilch1, int zilch2, double z, Vector& ret)
{
  int nnmax = ret.gethigh();
  int n=0;
  double zz = fabs(z);

  for (int nn=1; nn<=nnmax; nn++) {

    if (zz > zmax)
      ret[nn] = cnorm[nn]/sqrt(kstar[n]*kstar[n] + kx*kx) * cos(kstar[n]*zmax)
	* exp(-kx*(zz-zmax)) * kx * z/(zz+1.0e-10);
    else
      ret[nn] = cnorm[nn]/sqrt(kstar[n]*kstar[n] + kx*kx) * sin(kstar[n]*z)
	* kstar[n];

    if (nn++ == nnmax) return;

    if (zz > zmax)
      ret[nn] = cnorm[nn]/sqrt(kbstar[n]*kbstar[n] + kx*kx) * sin(kbstar[n]*zmax)
	* exp(-kx*(zz-zmax)) * kx * z/(zz+1.0e-10);
    else
      ret[nn] = -cnorm[nn]/sqrt(kbstar[n]*kbstar[n] + kx*kx) * cos(kbstar[n]*z)
	* kbstar[n];
    
    n++;
  }

}


double OneDTrig::get_potl(double z, int l, Vector& coef)
{
  int zilch1 = 0, zilch2 = 0;
  Vector ret(1, coef.gethigh());

  potl(zilch1, zilch2, z, ret);

  double ans = 0.0;
  int nmax = ret.gethigh();
  for (int n=1; n<=nmax; n++)
    ans += coef[n]*ret[n];

  return ans;
}


double OneDTrig::get_dens(double z, int l, Vector& coef)
{
  int zilch1=0, zilch2=0;
  Vector ret(1, coef.gethigh());

  dens(zilch1, zilch2, z, ret);

  double ans = 0.0;
  int nmax = ret.gethigh();
  for (int n=1; n<=nmax; n++)
    ans += coef[n]*ret[n];

  return ans;
}

double OneDTrig::get_force(double z, int l, Vector& coef)
{
  int zilch1=0, zilch2=0;
  Vector ret(1, coef.gethigh());

  force(zilch1, zilch2, z, ret);

  double ans = 0.0;
  int nmax = ret.gethigh();
  for (int n=1; n<=nmax; n++)
    ans += coef[n]*ret[n];

  return ans;
}


static double comp_kx;
static double comp_l;

static double find_tan(double k)
{
  return k*tan(k*comp_l) - comp_kx;
}

static double find_ctn(double k)
{
  return k/tan(k*comp_l) + comp_kx;
}

void OneDTrig::compute_kstar(int n)
{
  if (n<nrmax && n>0) return;

  nrmax = n;
  kstar.setsize(0, nrmax);
  kbstar.setsize(0, nrmax);

  comp_kx = kx;
  comp_l = zmax;

  double kmin, kmax;
  
  for (n=0; n<=nrmax; n++) {
    if (fabs(kx) > 1.0e-8) {
      kmin = M_PI*n/zmax;
      kmax = ((0.5 + n)*M_PI - KSTOL)/zmax;

      kstar[n] = zbrent(find_tan, kmin, kmax, KSZTOL);

      kmin = ((0.5 + n)*M_PI-KSTOL)/zmax;
      kmax = (M_PI*(1 + n)-KSTOL)/zmax;
      
      kbstar[n] = zbrent(find_ctn, kmin, kmax, KSZTOL);

    }
    else {
      kstar[n] = M_PI*n/zmax;
      kbstar[n] = (0.5 + n)*M_PI/zmax;
    }
    

  }

}

