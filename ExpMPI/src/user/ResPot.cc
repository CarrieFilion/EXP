#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <values.h>
#include <ResPot.H>
#include <ZBrent.H>
#include <localmpi.h>

#include <pthread.h>  
static pthread_mutex_t iolock = PTHREAD_MUTEX_INITIALIZER;

#define DEBUG_VERBOSE

double ResPot::ALPHA = 0.25;
double ResPot::DELTA = 0.01;
double ResPot::DELE = 0.001;
double ResPot::DELK = 0.001;
double ResPot::TOLITR = 1.0e-8;
int ResPot::NUME = 200;
int ResPot::NUMX = 200;
int ResPot::RECS = 100;
int ResPot::ITMAX = 50;
KComplex ResPot::I(0.0, 1.0);

const char* ResPot::ReturnDesc[] = {
  "OK", "CoordRad", "CoordVel", "CoordCirc", "CoordTP", "CoordRange",
  "UpdateBadL", "UpdateIterate", "UpdateBadVal"};


static Perturbation *pbar;
static double perturb(double r)
{
  return pbar->eval(r);
}

ResPot::ResPot(AxiSymModel *mod, Perturbation* pert,
	       int l, int m, int l1, int l2)
{
  halo_model = mod;
  
  grid_computed = false;
  second_order = true;
  
  L = l;
  M = m;
  L1 = l1;
  L2 = l2;
  
  Rmin = halo_model->get_min_radius();
  Rmax = halo_model->get_max_radius();
  Emax = halo_model->get_pot(Rmax)*(1.0+DELTA);
  Emin = halo_model->get_pot(Rmin)*(1.0-DELTA);
  Kmin = DELTA;
  Kmax = 1.0-DELTA;
  Kupd = 0.0;
  
  // SphericalOrbit::RMAXF=1.0;
  orb = new SphericalOrbit(halo_model);
  orb->set_numerical_params(RECS);
  
  pbar = pert;
  compute_grid();
}

ResPot::~ResPot()
{
  delete orb;
}

AxiSymModel *hmod;

// Circular orbit
//
static double get_rc(double r, double J)
{
  return J*J - hmod->get_mass(r)*r;
}

// Turning points
//
static double adj_r(double r, vector<double> p)
{
  return p[1] - 0.5*p[0]*p[0]/(r*r) - hmod->get_pot(r);
}


void ResPot::compute_grid() 
{
  if (grid_computed) return;
  
  double E, K;
  
  // Ang mom of minimum radius circular orbit
  minJ = Rmin*sqrt(max<double>(halo_model->get_dpot(Rmin), 0.0));
  
  // Ang mom of maximum radius circular orbit
  maxJ = Rmax*sqrt(max<double>(halo_model->get_dpot(Rmax), 0.0));
  
  // For derivatives
  double delE = (Emax - Emin)*0.5*DELE;
  double delK = (Kmax - Kmin)*0.5*DELK;
  
  double J, rcirc, Ecirc;
  
  dX = 1.0/(NUMX-1);
  
  hmod = halo_model;
  
  int iret;
  ZBrent<double> circ;
  
  for (int s=0; s<NUMX; s++) {
    
    // Unit interval to J
    J = Jx(double(s)/(NUMX-1), minJ, maxJ);
    
    // Compute Ecirc
    iret = circ.find(get_rc, J, Rmin, Rmax, 1.0e-10, rcirc);
    if (iret) {
      cerr << "Error locating circular orbit!!\n";
      exit(-1);
    }
    Ecirc = 0.5*J*J/(rcirc*rcirc) + halo_model->get_pot(rcirc);
    if (Ecirc<Emin) Ecirc = Emin;
    
    EminX.push_back(Ecirc);
    
    vector<double> IJ1, EJ1;
    ovector orbvec;
    
    // Grid from Ecirc to Emax
    double dE = (Emax - Ecirc)/(NUME - 1);
    
    for (int i=0; i<NUME; i++) {
      
      E = Ecirc + dE*i;
      
      orb->new_orbit(E, 0.5);
      K = J/orb->Jmax();
      K = max<double>(K, Kmin);
      K = min<double>(K, Kmax);
      
      RW rw;
      
      orb->new_orbit(E, K);
      rw.W = orb->pot_trans(L1, L2, perturb);
      
      struct ANGLE_GRID * grid = orb->get_angle_grid();
      
      for (int j=0; j<grid->num; j++) {
	rw.r.push_back(grid->r[1][j]);
	rw.w1.push_back(grid->w1[1][j]);
	rw.f.push_back(grid->f[1][j]);
      }
      rw.num = grid->num;
      rw.O1 = orb->get_freq(1);
      rw.O2 = orb->get_freq(2);
      rw.I1 = orb->get_action(1);
      rw.E = E;
      rw.K = K;
      rw.Jm = orb->Jmax();
      
      EJ1.push_back(rw.E);
      IJ1.push_back(rw.I1);
      
      
      // Derivatives
      // -----------
      
      double Ep=E+delE, Em=E-delE, Kp=K+delK, Km=K-delK;
      
      // Energy bounds
      if (E+delE > Emax) {
	Ep = Emax;
	Em = Emax - delE*2.0;
      }
      
      if (K-delK < Kmin) {
	Kp = Kmin + delK*2.0 ;
	Km = Kmin;
      }
      
      // Kappa bounds
      if (K+delK > Kmax) {
	Kp = Kmax;
	Km = Kmax - delK*2.0;
      }
      
      if (K-delK < Kmin) {
	Kp = Kmin + delK*2.0 ;
	Km = Kmin;
      }
      
      // Energy deriv
      orb->new_orbit(Ep, K);
      rw.dWE = orb->pot_trans(L1, L2, perturb);
      rw.dJm = orb->Jmax();
      
      orb->new_orbit(Em, K);
      rw.dWE = 0.5*(rw.dWE - orb->pot_trans(L1, L2, perturb))/delE;
      rw.dJm = 0.5*(rw.dJm - orb->Jmax())/delE;
      
      // Kappa deriv
      orb->new_orbit(E, Kp);
      rw.dWK = orb->pot_trans(L1, L2, perturb);
      
      orb->new_orbit(E, Km);
      rw.dWK = 0.5*(rw.dWK - orb->pot_trans(L1, L2, perturb))/delK;
      
      // Finally, store the data for this phase space point
      orbvec.push_back(rw);
      check_rw(rw);
    }
    
    // Normalize energy to [0,1]
    for (size_t j=0; j<EJ1.size(); j++) 
      EJ1[j] = (EJ1[j] - Ecirc)/(Emax - Ecirc);
    
    EX.push_back(EJ1);
    I1X.push_back(IJ1);
    
    orbmat.push_back(orbvec);
  }
  
  numx = EX.size();
  ngrid = orb->get_angle_grid()->num;
  
  grid_computed = true;
}


ResPot::ReturnCode ResPot::coord(double* ps1, double* vel,
				 double& E, double& K, double& I1, double& J,
				 double& O1, double& O2,
				 double& W1, double& W2, double& W3, 
				 double& F, double& BETA, double& PSI)
{
  double pos[3];
  for (int i=0; i<3; i++) pos[i] = ps1[i];
  if (fabs(ps1[2])<1.0e-8) pos[2] = 1.0e-8;
  
  // Compute polar coords
  // --------------------
  
  double r2=0.0, v2=0.0, rv=0.0, r;
  for (int i=0; i<3; i++) {
    r2 += pos[i]*pos[i];
    v2 += vel[i]*vel[i];
    rv += pos[i]*vel[i];
  }
  
  r = sqrt(r2);
  
  if (r>halo_model->get_max_radius()) return CoordRad;
  
  double theta = acos(pos[2]/r);
  double phi = atan2(pos[1], pos[0]);
  
  // Compute E, J, beta
  // ------------------
  
  E = 0.5*v2 + halo_model->get_pot(r);
  
  double angmom[3];
  
  angmom[0] = pos[1]*vel[2] - pos[2]*vel[1];
  angmom[1] = pos[2]*vel[0] - pos[0]*vel[2];
  angmom[2] = pos[0]*vel[1] - pos[1]*vel[0];
  
  J = 0.0;
  for (int i=0; i<3; i++) J += angmom[i]*angmom[i];
  J = sqrt(J);
  
  BETA = 0.0;
  if (J>0.0) BETA = acos(angmom[2]/J);
  
  
  if (fabs(rv)>1.0e-10*sqrt(r2*v2)) rv /= sqrt(r2*v2);
  else {			// Apocenter
    if (J*J/r2 < halo_model->get_mass(r))
      rv = -1.0;
    else			// Pericenter
      rv = 1.0;
  }
  
  // Linear interpolation coefficients
  // ---------------------------------
  
  J = max<double>(J, minJ);
  J = min<double>(J, maxJ);
  
  double X = xJ(J, minJ, maxJ);
  
  int indxX = (int)( X/dX );
  indxX = min<int>(indxX, numx-2);
  indxX = max<int>(indxX, 0);
  
  double cX[2];
  cX[0] = (dX*(indxX+1) - X)/dX;
  cX[1] = 1.0 - cX[0];
  
  double Y, Ecirc = cX[0]*EminX[indxX] + cX[1]*EminX[indxX+1];
  
  if (E<Ecirc) Y = 0.0;
  else if (E>Emax) Y = 1.0;
  else Y = (E - Ecirc)/(Emax - Ecirc);
  
  int indxE;
  double cE[2];
  for (int i1=0; i1<2; i1++) {
    indxE = Vlocate(Y, EX[indxX+i1]);
    indxE = min<int>(indxE, EX[indxX+i1].size()-2);
    indxE = max<int>(indxE, 0);
    
    cE[0] = (EX[indxX+i1][indxE+1] - Y)/
      (EX[indxX+i1][indxE+1] - EX[indxX+i1][indxE]);
    
    // Bounds: keep it on the grid else unphsical!
    cE[0] = max<double>(cE[0], 0.0);
    cE[0] = min<double>(cE[0], 1.0);
    
    cE[1] = 1.0 - cE[0];
    
  }
  
  
  // Compute angles
  // --------------
  
  double fac;
  int num;
  
  vector<double> tw(ngrid, 0.0);
  vector<double> tf(ngrid, 0.0);
  vector<double> tr(ngrid, 0.0);
  
  I1 = K = O1 = O2 = 0.0;
  
  for (int i1=0; i1<2; i1++) {
    for (int i2=0; i2<2; i2++) {
      
      RW *rw = &(orbmat[indxX+i1][indxE+i2]);
      num = rw->num;
      fac = cX[i1]*cE[i2];
      
      if (ngrid != num) {
	cerr << "ResPot::coord[1]: Oops! ngrid=" 
	     << ngrid << "  num=" << num 
	     << "  indxX=" << indxX+i1 << "/" << numx
	     << "  indxE=" << indxE+i2 << "/" << EX[indxX+i1].size()
	     << endl;
      }
      
      I1 += fac * rw->I1;
      K  += fac * rw->K;
      O1 += fac * rw->O1;
      O2 += fac * rw->O2;
      
      for (int k=0; k<ngrid; k++) {
	tw[k] += fac * rw->w1[k];
	tf[k] += fac * rw->f[k];
	tr[k] += fac * rw->r[k];
      }
      
    }
  }
  
  double w1, f;			// Enforce limits
  if (r < tr[0]) {
    w1 = tw[0];
    f  = tf[0];
  } else if (r > tr[ngrid-1]) {
    w1 = tw[ngrid-1];
    f  = tf[ngrid-1];
  } else {
    w1 = odd2(r, tr, tw);
    f  = odd2(r, tr, tf);
  }
  
  if (rv < 0.0) {
    w1 = 2.0*M_PI - w1;
    f *= -1.0;
  }
  
  
  // Angle computation
  // -----------------
  
  double psi, w3 = atan2(angmom[1], angmom[0]) + 0.5*M_PI;
  
  if (fabs(BETA)<1.0e-10) psi = phi - w3;
  else {
    double tmp = cos(theta)/sin(BETA);
    
    if (fabs(tmp)>1.0) {
      if (tmp>1.0) psi =  0.5*M_PI;
      else         psi = -0.5*M_PI;
    } 
    else psi = asin(tmp);
  }
  
  
  // Map Psi into [-Pi, Pi]
  // ----------------------
  double tmp = atan2(sin(phi - w3), cos(phi - w3));
  if (tmp>0.5*M_PI || tmp<-0.5*M_PI) {
    psi = M_PI - psi;
  }
  
  double w2 = psi + f;
  
  K = max<double>(K, Kmin);
  K = min<double>(K, Kmax);
  
  W1  = w1;
  W2  = w2;
  W3  = w3;
  F   = f;
  PSI = psi;
  
  return OK;
}


//
// Coordindate mapping to unit interval
//

double ResPot::xJ(double J, double Jmin, double Jmax)
{
  if (J>=Jmax) return 1.0;
  if (J<=Jmin) return 0.0;
  return pow( (J-Jmin)/(Jmax-Jmin), ALPHA );
}

double ResPot::Jx(double x, double Jmin, double Jmax)
{
  if (x>=1.0) return Jmax;
  if (x<=0.0) return Jmin;
  return Jmin + (Jmax - Jmin)*pow(x, 1.0/ALPHA);
}


double ResPot::dxJ(double J, double Jmin, double Jmax)
{
  if (J>=Jmax) return 1.0/(Jmax-Jmin);
  if (J<=0.0) return MAXFLOAT;
  return pow( (J-Jmin)/(Jmax-Jmin), ALPHA-1.0 )/(Jmax - Jmin);
}


void ResPot::getInterp(double I1, double I2, int& indxX, int& indxE,
		       double cX[2], double cE[2], bool& noboundary)
{
  noboundary = true;
  
  // Linear interpolation coefficients
  // ---------------------------------
  
  double X = xJ(I2, minJ, maxJ);
  
  indxX = (int)( X/dX );
  indxX = min<int>(indxX, numx-2);
  indxX = max<int>(indxX, 0);
  
  cX[0] = (dX*(indxX+1) - X)/dX;
  cX[1] = 1.0 - cX[0];
  
  if (isnan(cX[0]) || isnan(cX[1])) {
    cerr << "getInterp: cX is NaN, X=" << X 
	 << "  I1=" << I1
	 << "  I2=" << I2 
	 << "  minJ=" << minJ
	 << "  maxJ=" << maxJ
	 << endl;
  }
  
  double I1min = cX[0]*I1X[indxX].front()  + cX[1]*I1X[indxX+1].front();
  double I1max = cX[0]*I1X[indxX].back()   + cX[1]*I1X[indxX+1].back();
  
  // Assign Y index
  //
  if (I1<=I1min) {		// Below grid
    noboundary = false;
    indxE = 0;
    cE[0] = 1.0;
    cE[1] = 0.0;
  }
  else if (I1>=I1max) {		// Above grid
    noboundary = false;
    indxE = NUME-2;
    cE[0] = 0.0;
    cE[1] = 1.0;
  }
  else {			// Do binary search to get Y index
    int mid, lo=-1, hi=NUME;
    while ( hi-lo > 1 ) {
      mid=(hi+lo) >> 1;		// Divide by two
      if ( I1 > cX[0]*I1X[indxX][mid] + cX[1]*I1X[indxX+1][mid])
	lo = mid;		// Discard lower interval
      else
	hi = mid;		// Discard upper interval
    }
    
    if (lo==-1)
      indxE = 0;
    else
      indxE = lo;
    
    double Ilo = cX[0]*I1X[indxX][indxE]   + cX[1]*I1X[indxX+1][indxE];
    double Ihi = cX[0]*I1X[indxX][indxE+1] + cX[1]*I1X[indxX+1][indxE+1];
    
    cE[0] = (Ihi - I1)/(Ihi - Ilo);
    cE[1] = 1.0 - cE[0];
    
    // Test: should always be on grid
    //
    if (cE[0]<0.0 && cE[0]>1.0) {
      cout << "ResPot[id=" << myid 
	   << "]: WEIGHT out of bounds, indxE=" << indxE
	   << " cE[0]=" << cE[0]
	   << " cE[1]=" << cE[1]
	   << " Ilo, Ihi, I1=" << Ilo << ", " << Ihi << ", " << I1 << endl;
    }
    if (indxE<0 && indxE>NUME-1) {
      cout << "ResPot[id=" << myid 
	   << "]: INDEX out of bounds, indxE=" << indxE
	   << " cE[0]=" << cE[0]
	   << " cE[1]=" << cE[1]
	   << " Ilo, Ihi, I1=" << Ilo << ", " << Ihi << ", " << I1 << endl;
    }
  }
  
  return;
}

bool ResPot::getValues(double I1, double I2,
		       double& O1, double& O2)
{
  O1 = 0.0;
  O2 = 0.0;
  
  int indxX;
  int indxE;
  double cX[2];
  double cE[2];
  
  bool noboundary;
  getInterp(I1, I2, indxX, indxE, cX, cE, noboundary);
  
  // Interpolate frequencies
  // -----------------------
  
  double fac;
  int num;
  
  for (int i1=0; i1<2; i1++) {
    for (int i2=0; i2<2; i2++) {
      
      RW *rw = &(orbmat[indxX+i1][indxE+i2]);
      num = rw->num;
      fac = cX[i1]*cE[i2];
      
      if (ngrid != num) {
	cerr << "ResPot::getValues[1]: Oops! ngrid=" 
	     << ngrid << "  num=" << num 
	     << "  indxX=" << indxX+i1 << "/" << numx
	     << "  indxE=" << indxE+i2 << "/" << EX[indxX+i1].size()
	     << endl;
      }
      
      O1  += fac * rw->O1;
      O2  += fac * rw->O2;
    }
  }
  
  // return noboundary;
  return true;
}

bool ResPot::getValues(double I1, double I2,
		       double& O1, double& O2,
		       double& Jm, double& dJm,
		       KComplex& Ul, KComplex& dUldE, KComplex& dUldK)
{
  O1 = 0.0;
  O2 = 0.0;
  Jm = 0.0;
  dJm = 0.0;
  Ul = 0.0;
  dUldE = 0.0;
  dUldK = 0.0;
  
  
  int indxX;
  int indxE;
  double cX[2];
  double cE[2];
  
  bool wasok = true;
  
  if (isnan(I1) || isnan(I2)) {
    cerr << "NaN on input values to getInterp\n";
    wasok = false;
  }
  
  
  bool noboundary;
  getInterp(I1, I2, indxX, indxE, cX, cE, noboundary);
  
  
  for (int i1=0; i1<2; i1++) {
    if (indxE >= (int)EX[indxX+i1].size()-1) {
      cerr << "How did this happen?! [getValue[2]]\n";
      wasok = false;
    }
  }
  
  
  // Compute Ul and derivatives, Jmax, and frequencies
  // -------------------------------------------------
  
  double fac;
  int num;
  
  for (int i1=0; i1<2; i1++) {
    for (int i2=0; i2<2; i2++) {
      
      RW *rw = &(orbmat[indxX+i1][indxE+i2]);
      num = rw->num;
      fac = cX[i1]*cE[i2];
      
      if (isnan(fac)) {
	if (wasok) 
	  cerr << "ResPot::getValues[2]: fac=NaN and was OK!!  cX[" << i1 << "]=" << cX[i1]
	       << "  cE[" << i1 << "][" << i2 << "]=" << cE[i2] 
	       << "  indxX=" << indxX+i1 << "/" << numx
	       << "  indxE=" << indxE+i2 << "/" << EX[i1].size()-1
	       << endl;
	else
	  cerr << "ResPot::getValues[2]: fac=NaN!!  cX[" << i1 << "]=" << cX[i1]
	       << "  cE[" << i1 << "][" << i2 << "]=" << cE[i2] 
	       << "  indxX=" << indxX+i1 << "/" << numx
	       << "  indxE=" << indxE+i2 << "/" << EX[i1].size()-1
	       << endl;
      }
      
      if (ngrid != num) {
	assert( EX[indxX+i1].size() == I1X[indxX+i1].size() );
	cerr << "ResPot::getValues[2]: Oops! ngrid=" 
	     << ngrid << "  num=" << num 
	     << "  i1, i2=" << i1 << ", " << i2
	     << "  indxX=" << indxX+i1 << "/" << numx
	     << "  indxE=" << indxE+i2 << "/" << EX[indxX+i1].size()
	     << endl;
      }
      
      O1  += fac * rw->O1;
      O2  += fac * rw->O2;
      Jm  += fac * rw->Jm;
      dJm += fac * rw->dJm;
      
      Ul +=    fac * rw->W;
      dUldE += fac * rw->dWE;
      dUldK += fac * rw->dWK;
    }
  }
  
  // return noboundary;
  return true;
}


ResPot::ReturnCode ResPot::coord(double* pos, double* vel,
				 double I1, double I2, double beta,
				 double w1, double w2, double w3)
{
  // Linear interpolation coefficients
  // ---------------------------------
  
  int indxX;
  int indxE;
  double cX[2];
  double cE[2];
  
  bool noboundary;
  getInterp(I1, I2, indxX, indxE, cX, cE, noboundary);
  
  for (int i1=0; i1<2; i1++) {
    if (indxE >= (int)EX[indxX+i1].size()-1) {
      cerr << "How did this happen?! [after getInterp]\n";
    }
  }
  
  // Compute radius, f(w1)
  // --------------------
  
  double fac;
  int num;
  
  vector<double> tw(ngrid, 0.0);
  vector<double> tf(ngrid, 0.0);
  vector<double> tr(ngrid, 0.0);
  
  double E = 0.0, test = 0.0;
  double rmin=Rmax, rmax=Rmin;
  
  for (int i1=0; i1<2; i1++) {
    for (int i2=0; i2<2; i2++) {
      
      RW *rw = &(orbmat[indxX+i1][indxE+i2]);
      num = rw->num;
      fac = cX[i1]*cE[i2];
      test += fac;
      
      if (ngrid != num) {
	cerr << "ResPot::coord[2]: Oops! ngrid=" 
	     << ngrid << "  num=" << num
	     << "  indxX=" << indxX+i1 << "/" << numx
	     << "  indxE=" << indxE+i2 << "/" << EX[indxX+i1].size()
	     << endl;
      }
      
      E += fac * rw->E;
      
      for (int k=0; k<ngrid; k++) {
	tw[k] += fac * rw->w1[k];
	tf[k] += fac * rw->f[k];
	tr[k] += fac * rw->r[k];
      }
      
      rmin = min<double>(rmin, rw->r[0]);
      rmax = max<double>(rmax, rw->r[ngrid-1]);
    }
  }
  
  if ( fabs(test-1.0) >= 1.0e-10 ) {
    cout << "Test=" << test << endl;
  }
  
  assert( fabs(test-1.0)<1.0e-10 );
  
  // Wrap w_1 in [0, 2*pi]
  if (w1>=0.0)
    w1 -=  2.0*M_PI*(int)(0.5*w1/M_PI);
  else
    w1 +=  2.0*M_PI*((int)(-0.5*w1/M_PI) + 1);
  
  double w10 = w1, rv=1.0;
  if (w1>M_PI) {
    w10 = 2.0*M_PI - w1;
    rv = -1.0;
  }
  
  // Compute coordinates
  // -------------------
  
  double r    = odd2(w10, tw, tr);
  double f    = odd2(w10, tw, tf);
  double psi  = w2 - rv*f;
  
  double cosp = cos(psi);
  double sinp = sin(psi);
  
  double cosb = cos(beta);
  double sinb = sin(beta);
  
  double vtot = sqrt(fabs(2.0*(E - halo_model->get_pot(r))));
  double vt   = I2/r;
  
  if (vtot < vt) {		// Adjust r to make orbit valid
    double rcirc;
    ZBrent< double > circ;
    ZBrent< vector<double> > tp;
    ZBrentReturn ret;
    vector<double> param(2);
    param[0] = I2;
    param[1] = E;
    double fmin, fmax;
    const double ftol = 1.0e-3;
    
    if ( (ret=circ.find(get_rc, I2, 0.5*rmin, 2.0*rmax, 1.0e-10, rcirc)) ) {
#ifdef DEBUG_VERBOSE
      cout << "  Rcirc bounds error: val, rmin=" 
	   << get_rc(0.5*rmin, I2) << ", " << rmin
	   << "  val, rmax=" 
	   << get_rc(2.0*rmax, I2) << ", " << rmax << endl;
#endif
      return CoordCirc;
    }
    
    rv = (w1>=M_PI) ? -1.0 : 1.0;
    
    if (w1>1.5*M_PI || w1 < 0.5*M_PI) {	// Below circular orbit radius
      
      fmin = adj_r(Rmin,  param);
      fmax = adj_r(rcirc, param);
      
      if (fmin*fmax >= 0.0) {
	if (fabs(fmin) < ftol*fabs(fmax)) {
	  r = Rmin;
	} else if (fabs(fmax) < ftol*fabs(fmin)) {
	  r = rcirc;
	} else {
#ifdef DEBUG_VERBOSE	
	  cout << "  Inner turning point problem: E=" << E << "  J=" << I2
	       << "  val, r=" << fmin << ", " << Rmin
	       << "  val, rcirc=" << fmax << ", " << rcirc << endl;
#endif
	  return CoordRange;
	}
      } else {			// Above or at circular orbit radius
	
	if ( (ret=tp.find(adj_r, param, Rmin, rcirc, 1.0e-10, r)) ) {
#ifdef DEBUG_VERBOSE	
	  cout << "  Radj inner bounds error: E=" << E << "  J=" << I2
	       << "  val, r=" << adj_r(Rmin, param) 
	       << ", " << Rmin
	       << "  val, rcirc=" << adj_r(rcirc, param) 
	       << ", " << rcirc << endl;
#endif
	  return CoordTP;
	}
	
      }
      
    } else {
      
      fmin = adj_r(rcirc, param);
      fmax = adj_r(Rmax,  param);
      
      if (fmin*fmax >= 0.0) {
	if (fabs(fmin) < ftol*fabs(fmax)) {
	  r = rcirc;
	} else if (fabs(fmax) < ftol*fabs(fmin)) {
	  r = Rmax;
	} else {
#ifdef DEBUG_VERBOSE	
	  cout << "  Outer turning point problem: E=" << E << "  J=" << I2
	       << "  val, r=" << fmin << ", " << Rmin
	       << "  val, rcirc=" << fmax << ", " << rcirc << endl;
#endif
	  return CoordRange;
	}
      } else {
	
	if ( (ret=tp.find(adj_r, param, rcirc, Rmax, 1.0e-10, r)) ) {
#ifdef DEBUG_VERBOSE
	  cout << "  Radj outer bounds error: E=" << E << "  J=" << I2
	       << "  val, rcirc=" << adj_r(rcirc, param) << ", " << rcirc
	       << "  val, r=" << adj_r(Rmax, param) << ", " << Rmax << endl;
#endif
	  return CoordTP;
	}
	
      }
    }
    
    vtot = sqrt(fabs(2.0*(E - halo_model->get_pot(r))));
    vt = I2/r;
    f = 0.0;
    psi = w2;
  }
  
  double vr = 0.0;
  if (vtot > vt) vr = sqrt(vtot*vtot - vt*vt) * rv;
  
  double cost = sinp*sinb;
  double sint = sqrt(fabs(1.0 - cost*cost));
  
  // Check for excursion beyond bounds
  if (fabs(cost)>1.0) {
    cost = copysign(1.0, cost);
    sint = 0.0;
  }
  
  double cos3 = cos(w3);
  double sin3 = sin(w3);
  
  // Check for excursion beyond bounds
  double phi;
  double tmp  = atan2(sin(psi), cos(psi));
  
  if (fabs(sinb)>1.0e-8) {
    
    phi = cosb*cost/(sinb*sint);
    if (fabs(phi)>=1.0) phi = copysign(1.0, phi);
    phi  = asin(phi);
    
    // Phi branch based on Psi
    if (tmp>0.5*M_PI || tmp<-0.5*M_PI) phi = M_PI - phi;
    phi += w3;
    
  } else {
    phi = w3 + tmp;
  }
  
  double cosf = cos(phi);
  double sinf = sin(phi);
  
  pos[0] = r * sint*cosf;
  pos[1] = r * sint*sinf;
  pos[2] = r * cost;
  
  // Compute velocities
  // ------------------
  
  double xp = cosp*vr - sinp*vt;
  double yp = sinp*vr + cosp*vt;
  
  vel[0] = xp*cos3 - yp*cosb*sin3;
  vel[1] = xp*sin3 + yp*cosb*cos3;
  vel[2] =           yp*sinb;
  
  return OK;
}


ofstream* open_debug_file()
{
  ostringstream sout;
  sout << "update." << respot_mpi_id();
  return new ofstream(sout.str().c_str(), ios::app);
}

ResPot::ReturnCode ResPot::Update(double dt, 
				  vector<double>& Phase, 
				  double amp,
				  double* posI, double* velI,
				  double* posO, double* velO)
{
  if (M)
    return Update3(dt, Phase, amp, posI, velI, posO, velO);
  else
    return Update2(dt, Phase, amp, posI, velI, posO, velO);
}


ResPot::ReturnCode ResPot::Update2(double dt, 
				   vector<double>& Phase, 
				   double amp,
				   double* posI, double* velI,
				   double* posO, double* velO)
{
  ofstream* out = 0;
  
  compute_grid();
  
  ReturnCode ret = OK;
  
  if (L1==0 && L2==0) {
    for (int k=0; k<3; k++) {
      posO[k] = posI[k];
      velO[k] = velI[k];
    }
    return UpdateBadL;
  }
  
  // Get action angle coords
  //
  double E, W1, W2, W3, F, BETA, PSI;
  double I1, I2, O1, O2;
  ret = coord(posI, velI, E, Kupd, I1, I2, O1, O2, W1, W2, W3, F, BETA, PSI);
  if (ret != OK) return ret;
  
  double betaM, betaP, beta;
  if (BETA-DELTA<0.0) {
    betaM = 0.0;
    betaP = DELTA;
    beta = 0.5*DELTA;
  } else if (BETA+DELTA>M_PI) {
    betaM = M_PI - DELTA;
    betaP = M_PI;
    beta = M_PI - 0.5*DELTA;
  } else {
    betaM = BETA - DELTA;
    betaP = BETA + DELTA;
    beta = BETA;
  }
  
  KComplex VB = VeeBeta(L, L2, M, beta);
  
  KComplex DVB = 
    (VeeBeta(L, L2, M, betaP) - VeeBeta(L, L2, M, betaM)) / (betaP - betaM);
  
  // Iterative implicit solution
  //
  double Is [4] = {0.0, 0.0, 0.0, 0.0};
  double ws [4] = {0.0, 0.0, 0.0, 0.0};
  double wf [4] = {0.0, 0.0, 0.0, 0.0};
  //                ^    ^    ^    ^
  //                |    |    |    |
  //                |    |    |    \_ Penultimate (for convergence check)
  //                |    |    |
  //                |    |    \_ Latest iteration
  //                |    | 
  //                |    \_ Last step
  //                |
  //                \_ Initial step
  //
  double If, Jm, dJm, dEIs=0.0, dKIs=0.0, dKI1=0.0, dKI2=0.0, I3;
  KComplex Fw, FI, Ul, Ff, dUldE, dUldK, dUldI2, UldVdIs;
  bool done = false;
  
#ifdef DEBUG_VERBOSE
  double I10 = I1;
  double I20 = I2;
#endif
  
  if (isnan(I1) || isnan(I2)) {
    cerr << "Have a cow!\n";
  }
  
  // Transformation to slow-fast variables
  //
  
  ws[2]  = ws[0]  = W1*L1 + W2*L2 + (W3 - Phase[0])*M;
  if (L2) {
    Is[2] = Is[0] = I2/L2;
    wf[2] = wf[0] = W1;
    If = I1 - Is[0]*L1;
  } else {
    Is[2] = Is[0] = I1/L1;
    wf[2] = wf[0] = W2;
    If = I2 - Is[0]*L2;
  }
  
  I3 = I2*cos(BETA);
  
  double Omega = (Phase[2] - Phase[0])/dt;

  int i;
  for (i=0; i<ITMAX; i++) {
    
    // For convergence test
    //
    ws[3] = ws[1]; 
    wf[3] = wf[1];
    Is[3] = Is[1];
    
    
    // Save previous step
    //
    ws[1] = ws[2];
    wf[1] = wf[2];
    Is[1] = Is[2];
    
    // For force evaluation
    //
    if (second_order) {
      // Second order
      Is[2] = 0.5*(Is[2] + Is[0]);
      ws[2] = 0.5*(ws[2] + ws[0]);
      wf[2] = 0.5*(wf[2] + wf[0]);
      
    } else {
      // First order
      Is[2] = Is[2];
      ws[2] = ws[2];
      wf[2] = wf[2];
    }
    
    // Canonical transform
    //
    if (L2) {
      I1 = If + Is[2]*L1;
      I2 = Is[2]*L2;
    } else {
      I1 = Is[2]*L1;
      I2 = If + Is[2]*L2;
    }
    
    getValues(I1, I2, O1, O2, Jm, dJm, Ul, dUldE, dUldK);

    // Sanity check
    //
    if (isnan(I1) || isnan(I2)) {
      cerr << "I1 or I2 is NaN: Is0=" 
	   << Is[0] << " Is=" << Is << " If=" 
	   << If << " Is_2=" << Is[2] << " i=" 
	   << i << endl;
      
      pthread_mutex_lock(&iolock);
      out = open_debug_file();
      *out <<  "I1 or I2 is NaN: Is0=" 
	   << Is[0] << " Is1=" << Is[1] << " If=" 
	   << If << " Is2=" << Is[2] << " i=" 
	   << i << endl;
      
      out->close();
      pthread_mutex_unlock(&iolock);
      out = 0;
    }
    
    
    dUldI2 = Ul * DVB * amp / (tan(beta)*I2);
    UldVdIs = dUldI2 * L2;
    Ul *= VB * amp;
    dUldE *= VB * amp;
    dUldK *= VB * amp;
    
    dEIs = O1*L1 + O2*L2;
    dKIs = 1.0/Jm*L2;
    dKI1 = -I2*dJm/(Jm*Jm)*O1;
    dKI2 = 1.0/Jm - I2*dJm/(Jm*Jm)*O2;
    
    Fw = O1*L1 + O2*L2 - Omega*M +
      (dUldE*dEIs + dUldK*dKIs + UldVdIs)*exp(I*ws[2]);
    FI = -I*Ul*exp(I*ws[2]);
    if (L2)
      Ff = O1 + (dUldE*O1 + dUldK*dKI1)*exp(I*ws[2]);
    else
      Ff = O2 + (dUldE*O2 + dUldK*dKI2 + dUldI2)*exp(I*ws[2]);
    
    // Sanity check
    //
    if (
	isnan(Fw.real()) || isnan(FI.real()) ||
	isnan(Ff.real())
	) {
      cerr << "Fw or FI is NaN, dJm=" << dJm 
	   << " Ul="	<< Ul 
	   << " dUldE="	<< dUldE 
	   << " dUldK="	<< dUldK 
	   << " dEIs="	<< dEIs 
	   << " dKIs="	<< dKIs 
	   << " O1="	<< O1 
	   << " O2="	<< O2 
	   << " dt="	<< dt
	   << " ws="	<< ws[1]
	   << " ws0="	<< ws[0]
	   << " ws2="	<< ws[2]
	   << " i="	<< i << endl;
      
      pthread_mutex_lock(&iolock);
      out = open_debug_file();
      *out  << "Fw or FI is NaN, dJm=" << dJm 
	    << " Ul="	<< Ul 
	    << " dUldE="	<< dUldE 
	    << " dUldK="	<< dUldK 
	    << " dEIs="	<< dEIs 
	    << " dKIs="	<< dKIs 
	    << " O1="	<< O1 
	    << " O2="	<< O2 
	    << " dt="	<< dt
	    << " ws="	<< ws[1]
	    << " ws0="	<< ws[0]
	    << " ws2="	<< ws[2]
	    << " i="	<< i << endl;
      out->close();
      pthread_mutex_unlock(&iolock);
      out = 0;
    }
    
    // Update
    //
    ws[2] = ws[0] + dt*Fw.real();
    Is[2] = Is[0] + dt*FI.real();
    wf[2] = wf[0] + dt*Ff.real();
    
    if (isnan(ws[2])) {
      cerr << "ws2 is NaN, Fw=" << Fw.real()
	   << " ws0=" << ws[0]
	   << " dt=" << dt
	   << " i="	<< i << endl;
      
      pthread_mutex_lock(&iolock);
      out = open_debug_file();
      *out  << "ws2 is NaN, Fw=" << Fw.real()
	    << " ws0=" << ws[0]
	    << " dt=" << dt
	    << " i="	<< i << endl;
      out->close();
      pthread_mutex_unlock(&iolock);
      out = 0;
    }
    
    
    // Check for convergence
    //
    if (fabs(ws[1]-ws[2])<TOLITR*dt && 
	fabs(Is[1]-Is[2])/(fabs(Is[2])+1.0e-10)<TOLITR*dt &&
	fabs(wf[1]-wf[2])<TOLITR*dt
	) done = true;
    
    // Limit 2-cycle detection
    //
    if (i>3 &&
	fabs(ws[3]-ws[2])<TOLITR*dt && 
	fabs(Is[3]-Is[2])/(fabs(Is[2])+1.0e-10)<TOLITR*dt) done = true;
    
    if (done) break;
    
  }
  
  if (!done) {
    // #ifdef DEBUG
    cerr << endl
	 << "Not done: "
	 << "Phase, E, K, I1, I2, DI, Dw, Ul, dUldE, dUldK, dEIs, dKIs = " 
	 << Phase[1]
	 << ", " << E
	 << ", " << Kupd
	 << ", " << I1
	 << ", " << I2
	 << ", " << Is[2]-Is[1]
	 << ", " << ws[2]-ws[1]
	 << ", " << Is[2]-Is[3]
	 << ", " << ws[2]-ws[3]
	 << ", " << Ul
	 << ", " << dUldE
	 << ", " << dUldK
	 << ", " << dEIs
	 << ", " << dKIs
	 << endl;
    // #endif
    ret = UpdateIterate;
  }
  
  // Canonical transformation from slow-fast to action-angle
  // 
  if (L2) {
    W1 = wf[2];
    W2 = (ws[2] - W1*L1 + Phase[2]*M)/L2;
    I1 = Is[2]*L1 + If;
    I2 = Is[2]*L2;
  } else {
    W2 = wf[2];
    W1 = (ws[2] - W2*L2 + Phase[2]*M)/L2;
    I1 = Is[2]*L1;
    I2 = Is[2]*L2 + If;
  }
  
  double cosb = I3/I2;
  cosb = min<double>(cosb,  1.0);
  cosb = max<double>(cosb, -1.0);
  BETA = acos(cosb);
  
#ifdef DEBUG_VERBOSE
  if (fabs(I10-I1)>1.0e-3*fabs(I10) || fabs(I20-I2)>1.0e-3*fabs(I20)) {
    cout << setw(15) << I10
	 << setw(15) << I1
	 << setw(15) << I20
	 << setw(15) << I2
	 << setw(15) << fabs(I10 - I1)
	 << setw(15) << fabs(I20 - I2)
	 << endl;
  }
#endif
  
  // Get new Cartesion phase space
  //
  ret = coord(posO, velO, I1, I2, BETA, W1, W2, W3); 
  if (ret != OK) {
    for (int k=0; k<3; k++) {
      posO[k] = posI[k];
      velO[k] = velI[k];
    }
  }
  
  // Debug
  for (int k=0; k<3; k++) {
    if (isnan(posO[k]) || isinf(posO[k]) || isnan(velO[k]) || isinf(velO[k]))
      ret = UpdateBadVal;
  }
  
  return ret;
}


ResPot::ReturnCode ResPot::Update3(double dt, 
				   vector<double>& Phase, 
				   double amp, 
				   double* posI, double* velI,
				   double* posO, double* velO)
{
  ofstream* out = 0;
  
  compute_grid();
  
  ReturnCode ret;
  
  if (L1==0 && L2==0) {
    for (int k=0; k<3; k++) {
      posO[k] = posI[k];
      velO[k] = velI[k];
    }
    return UpdateBadL;
  }
  
  // Get action angle coords
  //
  double E, W1, W2, W3, F, BETA, PSI;
  double I1, I2, O1, O2;
  if ((ret=coord(posI, velI, E, Kupd, I1, I2, O1, O2, W1, W2, W3, F, BETA, PSI)) != OK) return ret;
  
  double betaM, betaP, beta;
  if (BETA-DELTA<0.0) {
    betaM = 0.0;
    betaP = DELTA;
    beta = 0.5*DELTA;
  } else if (BETA+DELTA>M_PI) {
    betaM = M_PI - DELTA;
    betaP = M_PI;
    beta = M_PI - 0.5*DELTA;
  } else {
    betaM = BETA - DELTA;
    betaP = BETA + DELTA;
    beta = BETA;
  }
  
  KComplex VB = VeeBeta(L, L2, M, beta);
  
  KComplex DVB = 
    (VeeBeta(L, L2, M, betaP) - VeeBeta(L, L2, M, betaM)) / (betaP - betaM);
  
  // Iterative implicit solution
  //
  double Is [4] = {0.0, 0.0, 0.0, 0.0};
  double ws [4] = {0.0, 0.0, 0.0, 0.0};
  double wf1[4] = {0.0, 0.0, 0.0, 0.0};
  double wf2[4] = {0.0, 0.0, 0.0, 0.0};
  //                ^    ^    ^    ^
  //                |    |    |    |
  //                |    |    |    \_ Penultimate (for convergence check)
  //                |    |    |
  //                |    |    \_ Latest iteration
  //                |    | 
  //                |    \_ Last step
  //                |
  //                \_ Initial step
  //
  double If1, If2;
  double Jm, dJm, dEIs=0.0, dKIs=0.0, dKI1=0.0, dKI2=0.0, I3, I30;
  KComplex Fw, FI, Ul, F1, F2, dUldE, dUldK, dUldb;
  bool done = false;
  
#ifdef DEBUG_VERBOSE
  double I10 = I1;
  double I20 = I2;
#endif
  
  if (isnan(I1) || isnan(I2)) {
    cerr << "Have a cow!\n";
  }
  
  // Transformation to slow-fast variables
  //
  
  ws[2]  = ws[0]  = W1*L1 + W2*L2 + (W3 - Phase[0])*M;
  wf1[2] = wf1[0] = W1;
  wf2[2] = wf2[0] = W2;
  
  I30 = I2*cos(beta);
  
  Is[2] = Is[0] = I30/M;
  If1 = I1 - Is[0]*L1;
  If2 = I2 - Is[0]*L2;
  
  
  double Omega = (Phase[2] - Phase[0])/dt;

  
  int i;
  for (i=0; i<ITMAX; i++) {
    
    // For convergence test
    //
    ws [3] = ws [1]; 
    wf1[3] = wf1[1];
    wf2[3] = wf2[1];
    Is [3] = Is [1];
    
    
    // Save previous step
    //
    ws [1] = ws [2];
    wf1[1] = wf1[2];
    wf2[1] = wf2[2];
    Is [1] = Is [2];
    
    // For force evaluation
    //
    if (second_order) {
      // Second order
      Is[2]  = 0.5*(Is[2]  + Is[0]);
      ws[2]  = 0.5*(ws[2]  + ws[0]);
      wf1[2] = 0.5*(wf1[2] + wf1[0]);
      wf2[2] = 0.5*(wf1[2] + wf1[0]);
      
    } else {
      // First order
      Is[2] = Is[2];
      ws[2] = ws[2];
      wf1[2] = wf1[2];
      wf2[2] = wf2[2];
    }
    
    // Canonical transform
    //
    I1 = If1 + Is[2]*L1;
    I2 = If2 + Is[2]*L2;
    
    getValues(I1, I2, O1, O2, Jm, dJm, Ul, dUldE, dUldK);

    // Sanity check
    //
    if (isnan(I1) || isnan(I2)) {
      cerr << "I1 or I2 is NaN: Is0=" 
	   << Is[0] << " Is=" << Is << " If1=" 
	   << If1 << " If2=" << If2 << " Is_2=" << Is[2] << " i=" 
	   << i << endl;
      
      pthread_mutex_lock(&iolock);
      out = open_debug_file();
      *out <<  "I1 or I2 is NaN: Is0=" 
	   << Is[0] << " Is1=" << Is[1] << " If1=" 
	   << If1 << " If2=" << If2 << " Is2=" << Is[2] << " i=" 
	   << i << endl;
      
      out->close();
      pthread_mutex_unlock(&iolock);
      out = 0;
    }
    
    
    dUldb = -Ul * DVB * amp * M / (sin(beta)*I2);
    Ul *= VB * amp;
    dUldE *= VB * amp;
    dUldK *= VB * amp;
    
    dEIs = O1*L1 + O2*L2;
    dKIs = 1.0/Jm*L2;
    dKI1 = -I2*dJm/(Jm*Jm)*O1;
    dKI2 = 1.0/Jm - I2*dJm/(Jm*Jm)*O2;
    
    Fw = O1*L1 + O2*L2 - Omega*M + dUldb*exp(I*ws[2]);
    FI = -I*Ul*exp(I*ws[2]);
    F1 = O1 + (dUldE*O1 + dUldK*dKI1)*exp(I*ws[2]);
    F2 = O2 + (dUldE*O2 + dUldK*dKI2)*exp(I*ws[2]);
    
    // Sanity check
    //
    if (
	isnan(Fw.real()) || isnan(FI.real()) ||
	isnan(F1.real()) || isnan(F2.real())
	) {
      cerr << "Fw or FI is NaN, dJm=" << dJm 
	   << " Ul="	<< Ul 
	   << " dUldE="	<< dUldE 
	   << " dUldK="	<< dUldK 
	   << " dEIs="	<< dEIs 
	   << " dKIs="	<< dKIs 
	   << " O1="	<< O1 
	   << " O2="	<< O2 
	   << " dt="	<< dt
	   << " ws="	<< ws[1]
	   << " ws0="	<< ws[0]
	   << " ws2="	<< ws[2]
	   << " i="	<< i << endl;
      
      pthread_mutex_lock(&iolock);
      out = open_debug_file();
      *out  << "Fw or FI is NaN, dJm=" << dJm 
	    << " Ul="	<< Ul 
	    << " dUldE="	<< dUldE 
	    << " dUldK="	<< dUldK 
	    << " dEIs="	<< dEIs 
	    << " dKIs="	<< dKIs 
	    << " O1="	<< O1 
	    << " O2="	<< O2 
	    << " dt="	<< dt
	    << " ws="	<< ws[1]
	    << " ws0="	<< ws[0]
	    << " ws2="	<< ws[2]
	    << " i="	<< i << endl;
      out->close();
      pthread_mutex_unlock(&iolock);
      out = 0;
    }
    
    // Update
    //
    ws[2]  = ws[0]  + dt*Fw.real();
    Is[2]  = Is[0]  + dt*FI.real();
    wf1[2] = wf1[0] + dt*F1.real();
    wf2[2] = wf2[0] + dt*F2.real();
    
    if (isnan(ws[2])) {
      cerr << "ws2 is NaN, Fw=" << Fw.real()
	   << " ws0=" << ws[0]
	   << " dt=" << dt
	   << " i="	<< i << endl;
      
      pthread_mutex_lock(&iolock);
      out = open_debug_file();
      *out  << "ws2 is NaN, Fw=" << Fw.real()
	    << " ws0=" << ws[0]
	    << " dt=" << dt
	    << " i="	<< i << endl;
      out->close();
      pthread_mutex_unlock(&iolock);
      out = 0;
    }
    
    
    // Check for convergence
    //
    if (fabs(ws[1]-ws[2])<TOLITR*dt && 
	fabs(Is[1]-Is[2])/(fabs(Is[2])+1.0e-10)<TOLITR*dt &&
	fabs(wf1[1]-wf1[2])<TOLITR*dt && 
	fabs(wf2[1]-wf2[2])<TOLITR*dt
	) done = true;
    
    // Limit 2-cycle detection
    //
    if (i>3 &&
	fabs(ws[3]-ws[2])<TOLITR*dt && 
	fabs(Is[3]-Is[2])/(fabs(Is[2])+1.0e-10)<TOLITR*dt) done = true;
    
    if (done) break;
    
  }
  
  if (!done) {
    // #ifdef DEBUG
    cerr << endl
	 << "Not done: "
	 << "Phase, E, K, I1, I2, DI, Dw, Ul, dUldE, dUldK, dEIs, dKIs = " 
	 << Phase[1]
	 << ", " << E
	 << ", " << Kupd
	 << ", " << I1
	 << ", " << I2
	 << ", " << Is[2]-Is[1]
	 << ", " << ws[2]-ws[1]
	 << ", " << Is[2]-Is[3]
	 << ", " << ws[2]-ws[3]
	 << ", " << Ul
	 << ", " << dUldE
	 << ", " << dUldK
	 << ", " << dEIs
	 << ", " << dKIs
	 << endl;
    // #endif
    
    ret = UpdateIterate;
  }
  
  // Canonical transformation from slow-fast to action-angle
  // 
  W1 = wf1[2];
  W2 = wf2[2];
  W3 = (ws[2] - W1*L1 - W2*L2 + Phase[2]*M)/M;
  I1 = Is[2]*L1 + If1;
  I2 = Is[2]*L2 + If2;
  I3 = Is[2]*M;
  
  double cosb = I3/I2;
  cosb = min<double>(cosb,  1.0);
  cosb = max<double>(cosb, -1.0);
  BETA = acos(cosb);
  
#ifdef DEBUG_VERBOSE
  if (fabs(I10-I1)>1.0e-3*fabs(I10) || fabs(I20-I2)>1.0e-3*fabs(I20)) {
    cout << setw(15) << I10
	 << setw(15) << I1
	 << setw(15) << I20
	 << setw(15) << I2
	 << setw(15) << fabs(I10 - I1)
	 << setw(15) << fabs(I20 - I2)
	 << endl;
  }
#endif
  
  // Get new Cartesion phase space
  //
  if ((ret=coord(posO, velO, I1, I2, BETA, W1, W2, W3)) != OK) {
    for (int k=0; k<3; k++) {
      posO[k] = posI[k];
      velO[k] = velI[k];
    }
  }
  
  // Debug
  for (int k=0; k<3; k++) {
    if (isnan(posO[k]) || isinf(posO[k]) || isnan(velO[k]) || isinf(velO[k]))
      ret = UpdateBadVal;
  }
  
  
  return ret;
}


ResPot::ReturnCode ResPot::Force(double dt, 
				 vector<double>& Phase, 
				 double amp,
				 double* pos, double* vel, double* acc)
{
  ReturnCode ret = OK;
  double pos0[3], vel0[3], pos2[3], vel2[3];
  double E, K, W1, W2, W3, F, BETA, PSI, I1, I2, O1, O2;
  
  // Get action angle coords
  ret = coord(pos, vel, E, K, I1, I2, O1, O2, W1, W2, W3, F, BETA, PSI);
  
  if (ret != OK) {
    for (int k=0; k<3; k++) acc[k] = 0.0;
    return ret;
  }
  
  // Get phase space update without perturbation
  W1 += O1*dt;
  W2 += O2*dt;
  ret = coord(pos0, vel0, I1, I2, BETA, W1, W2, W3);
  
  if (ret != OK) {
    for (int k=0; k<3; k++) acc[k] = 0.0;
    return ret;
  }
  
  // Get phase space update with perturbation
  ret = Update(dt, Phase, amp, pos, vel, pos2, vel2);
  
  if (ret != OK) {
    for (int k=0; k<3; k++) acc[k] = 0.0;
    return ret;
  }
  
  // Effective acceleration
  for (int k=0; k<3; k++) acc[k] = (vel2[k] - vel0[k])/dt;
  
  return ret;
}


void ResPot::check_rw(RW& rw)
{
  for (unsigned i=0; i<rw.r.size(); i++)
    if (isnan(rw.r[i])) cout << "RW error: r nan, i=" << i << endl;
  
  for (unsigned i=0; i<rw.w1.size(); i++)
    if (isnan(rw.w1[i])) cout << "RW error: w1 nan, i=" << i << endl;
  
  for (unsigned i=0; i<rw.f.size(); i++)
    if (isnan(rw.f[i])) cout << "RW error: f nan, i=" << i << endl;
  
  if (isnan(rw.O1))	cout << "RW error: O1 nan" << endl;
  if (isnan(rw.O2))	cout << "RW error: O2 nan" << endl;
  if (isnan(rw.Jm))	cout << "RW error: Jm nan" << endl;
  if (isnan(rw.dJm))	cout << "RW error: dJm nan" << endl;
  if (isnan(rw.E))	cout << "RW error: E nan" << endl;
  if (isnan(rw.K))	cout << "RW error: K nan" << endl;
  if (isnan(rw.I1))	cout << "RW error: I1 nan" << endl;
  if (isnan(rw.W))	cout << "RW error: W nan" << endl;
  if (isnan(rw.dWE))	cout << "RW error: dWE nan" << endl;
  if (isnan(rw.dWK))	cout << "RW error: dWK nan" << endl;
}

