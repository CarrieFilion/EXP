#include <math.h>
#include "expand.h"
#include <localmpi.h>

#include <SatelliteOrbit.h>
#include <AxisymmetricBasis.H>
#include <ExternalCollection.H>
#include <ResPot.H>
#include <biorth.h>
#include <sphereSL.h>
#include <UserResPotN.H>
#include <BarForcing.H>
#include <CircularOrbit.H>

#include <sstream>

#include <pthread.h>  
#ifdef DEBUG
static pthread_mutex_t iolock = PTHREAD_MUTEX_INITIALIZER;
#endif

UserResPotN::UserResPotN(string &line) : ExternalForce(line)
{
  LMAX = 2;
  NMAX = 20;
  NUMR = 800;
  L0 = 2;
  M0 = 2;
  Klim = 1.0;
  scale = 0.067;
  drfac = 0.05;

  ton = -20.0;			// Turn on time
  toff = 1.0e20;		// Turn off time
  delta = 1.0;			// Turn on duration
  toffset = 0.0;		// Time offset for orbit
  omega = -1.0;			// Patern speed
  phase0 = 0.0;			// Initial phase

  NUMX = 400;			// Points in Ang mom grid
  NUME = 200;			// Points in Energy
  RECS = 100;			// Points in Angle grid
  ITMAX = 50;			// Number of iterations for mapping solution
  DELE = 0.001;			// Fractional offset in E grid
  DELK = 0.001;			// Offset in Kappa
  DELB = 0.001;			// Offset in Beta
  ALPHA = 0.25;			// Power law index for J distribution

  MASS = -1.0;			// Bar mass
  MFRAC = 0.05;			// Fraction of enclosed mass
  LENGTH = 0.067;		// Bar length
  AMP = 1.0;			// Mass prefactor
  COROT = 10;			// Corotation factor
  A21 = 0.2;			// Major to semi-minor ratio
  A32 = 0.05;			// Semi-minor to minor ratio

  self = true;			// Self consistent slow down
  domega = 0.0;			// Rate of forced slow down
  tom0 = 1.0;			// Midpoint of forced bar slow down
  dtom = -1.0;			// Width of forced bar slow down
  fileomega = "";		// File containing Omega vs T

  usebar = true;		// Use BarForcing and 
  useorb = false;		// Not CircularOrbit

  first = true;
  debug = false;		// Diagnostic output

  usetag = -1;			// Flag not used unless explicitly defined

  pmass = -1.0;			// Mass for two-body diffusion
  diffuse = 0;

				// Tabled spherical model
  model_file = "SLGridSph.model";
  ctr_name = "";		// Default component for com is none

				// Log file name
  filename = outdir + "ResPot." + runtag;

  initialize();

  if (numRes==0)  {
    if (myid==0) cerr << "You must specify at least one resonance!\n";
    MPI_Abort(MPI_COMM_WORLD, 120);
    exit(0);
  }

  if (ctr_name.size()>0) {
				// Look for the fiducial component for
				// centering
    bool found = false;
    list<Component*>::iterator cc;
    Component *c;
    for (cc=comp.components.begin(); cc != comp.components.end(); cc++) {
      c = *cc;
      if ( !ctr_name.compare(c->name) ) {
	c0 = c;
	found = true;
      break;
      }
    }

    if (!found) {
      cerr << "Process " << myid << ": can't find desired component <"
	   << ctr_name << ">" << endl;
    }

  }
  else
    c0 = NULL;

				// Set up for resonance potential
  SphericalModelTable *hm = new SphericalModelTable(model_file);
  halo_model = hm;

				// Perturbation
  if (MASS < 0.0) MASS = hm->get_mass(LENGTH);

  if (usebar) {
    BarForcing::L0 = L0;
    BarForcing::M0 = M0;
    BarForcing *bar;
    if (MFRAC>0.0)
      bar = new BarForcing(NMAX, MFRAC*MASS, LENGTH, COROT);
    else
      bar = new BarForcing(NMAX, MASS, LENGTH, COROT);

    bar->set_model(halo_model);
    bar->compute_quad_parameters(A21, A32);
    omega = omega0 = bar->Omega();
    Iz = bar->get_Iz();

    pert = bar;
  } else {
    CircularOrbit *orb = new CircularOrbit(NMAX, L0, M0, MASS, LENGTH);

    if(omega <= 0.0)
      omega = omega0 = sqrt(MASS/(LENGTH*LENGTH*LENGTH));
    else
      omega0 = omega;

    Iz = MASS*LENGTH*LENGTH*omega;

    pert = orb;
  }

  ResPot::NUMX = NUMX;
  ResPot::NUME = NUME;
  ResPot::RECS = RECS;
  ResPot::ALPHA = ALPHA;
  ResPot::ITMAX = ITMAX;
  ResPot::DELTA_E = DELE;
  ResPot::DELTA_K = DELK;
  ResPot::DELTA_B = DELB;
				// Instantiate one for each resonance
  for (int i=0; i<numRes; i++) {
    respot.push_back(new ResPot(halo_model, pert, L0, M0, L1[i], L2[i]));
  }
				// Construct debug file names
  for (int i=0; i<numRes; i++) {
    ostringstream sout;
    sout << outdir << runtag 
	 << ".respot_dbg." << L1[i] << "_" << L2[i] << "." << myid;
    respot[i]->set_debug_file(sout.str());
  }

  // Initialize two-body diffusion
  if (pmass>0.0) {
    diffuse = new TwoBodyDiffuse (pmass);
    if (debug) {
      ostringstream file;
      file << outdir << "diffusion_grid." << runtag << "." << myid;
      ofstream out(file.str().c_str());
      if (out) diffuse->dump_grid(&out);
    }
  }

  btotn = vector<int>(ResPot::NumDesc-1);
  difLz0 = vector<double>(numRes);
  bcount = vector< vector<int> >(nthrds);
  difLz = vector< vector<double> >(nthrds);
  for (int i=0; i<nthrds; i++) {
    difLz[i] = vector<double>(numRes);
    bcount[i] = vector<int>(ResPot::NumDesc-1);
  }

				// Read omega file
  if (fileomega.size()) {
    ifstream in(fileomega.c_str());
    const int sizebuf = 1024;
    char linebuf[sizebuf];

    double t, om;
    if (in) {

      while (in) {
	in.getline(linebuf, sizebuf);
	if (!in) break;
	if (linebuf[0]=='#') continue;

	istringstream sin(linebuf);
	sin >> t;
	sin >> om;
	if (sin) {
	  Time.push_back(t);
	  Omega.push_back(om);
	}
      }

      omega = omega0 = Omega.front();

    } else {
      cout << "UserResPotN could not open <" << fileomega << ">\n";
      MPI_Abort(MPI_COMM_WORLD, 103);
    }
    
  }

  userinfo();
}

UserResPotN::~UserResPotN()
{
  for (int i=0; i<numRes; i++) delete respot[i];
  delete halo_model;
  delete pert;
  delete diffuse;
}

void UserResPotN::userinfo()
{
  if (myid) return;		// Return if node master node
  print_divider();
  cout << "** User routine RESONANCE POTENTIAL initialized";
  if (usebar) cout << " using bar";
  else cout << " using satellite";
  cout << " with Length=" << LENGTH 
       << ", Mass=" << MASS 
       << ", Mfrac=" << MFRAC 
       << ", Amp=" << AMP
       << ", Iz=" << Iz
       << ", Omega=" << omega 
       << ", b/a=" << A21
       << ", c/b=" << A32
       << ", Ton=" << ton
       << ", Toff=" << toff
       << ", Delta=" << delta
       << ", L=" << L0
       << ", M=" << M0
       << ", Klim=" << Klim
       << ", model=" << model_file;
  if (self)  cout << ", with self-consistent slow down";
  else if (fileomega.size())
    cout << ", using table <" << fileomega << "> for Omega(t)";
  else {
    if (dtom>0) cout << ", T_om=" << tom0 << ", dT_om=" << dtom;
    cout << ", Domega=" << domega;
  }
  if (usetag>=0)
    cout << ", with bad value tagging";
  for (int ir=0; ir<numRes; ir++)
    cout << ", (l_1,l_2)_" << ir << "=(" << L1[ir] << "," << L2[ir] << ")";
  cout << ", ITMAX=" << ITMAX;
  if (pmass>0.0) cout << ", using two-body diffusion with logL=5.7 and mass=" 
		      << pmass;
  cout << endl;
  print_divider();
}

void UserResPotN::initialize()
{
  string val;

  if (get_value("LMAX", val))     LMAX = atoi(val.c_str());
  if (get_value("NMAX", val))     NMAX = atoi(val.c_str());
  if (get_value("NUMR", val))     NUMR = atoi(val.c_str());

  if (get_value("L0", val))       L0 = atoi(val.c_str());
  if (get_value("M0", val))       M0 = atoi(val.c_str());

  for (numRes=0; numRes<1000; numRes++) {
    ostringstream countl1, countl2;
    countl1 << "L1(" << numRes+1 << ")";
    countl2 << "L2(" << numRes+1 << ")";
    if (get_value(countl1.str(), val)) {
      L1.push_back(atoi(val.c_str()));
      if (get_value(countl2.str(), val))
	L2.push_back(atoi(val.c_str()));
      else break;
    } else break;
  }

  if (L1.size() != L2.size() || numRes != (int)L1.size()) {
    cerr << "UserResPotN: error parsing resonances, "
	 << "  Size(L1)=" << L1.size() << "  Size(L2)=" << L2.size() 
	 << "  numRes=" << numRes << endl;
    MPI_Abort(MPI_COMM_WORLD, 119);
  }

  if (get_value("Klim", val))     Klim = atof(val.c_str());
  if (get_value("scale", val))    scale = atof(val.c_str());
  if (get_value("drfac", val))    drfac = atof(val.c_str());

  if (get_value("ton", val))      ton = atof(val.c_str());
  if (get_value("toff", val))     toff = atof(val.c_str());
  if (get_value("delta", val))    delta = atof(val.c_str());
  if (get_value("toffset", val))  toffset = atof(val.c_str());
  if (get_value("phase0", val))   phase0 = atof(val.c_str());

  if (get_value("MASS", val))     MASS = atof(val.c_str());
  if (get_value("MFRAC", val))    MFRAC = atof(val.c_str());
  if (get_value("LENGTH", val))   LENGTH = atof(val.c_str());
  if (get_value("AMP", val))      AMP = atof(val.c_str());
  if (get_value("COROT", val))    COROT = atof(val.c_str());
  if (get_value("A21", val))      A21 = atof(val.c_str());
  if (get_value("A32", val))      A32 = atof(val.c_str());

  if (get_value("NUMX", val))     NUMX = atoi(val.c_str());
  if (get_value("NUME", val))     NUME = atoi(val.c_str());
  if (get_value("RECS", val))     RECS = atoi(val.c_str());
  if (get_value("ITMAX", val))    ITMAX = atoi(val.c_str());
  if (get_value("DELE", val))     DELE = atof(val.c_str());
  if (get_value("DELK", val))     DELK = atof(val.c_str());
  if (get_value("DELB", val))     DELB = atof(val.c_str());
  if (get_value("ALPHA", val))    ALPHA = atof(val.c_str());
  
  if (get_value("self", val))     self = atoi(val.c_str());
  if (get_value("omega", val))    omega = atof(val.c_str());
  if (get_value("domega", val))   domega = atof(val.c_str());
  if (get_value("tom0", val))     tom0 = atof(val.c_str());
  if (get_value("dtom", val))     dtom = atof(val.c_str());

  if (get_value("pmass", val))    pmass = atof(val.c_str());


  if (get_value("model", val))    model_file = val;
  if (get_value("ctrname", val))  ctr_name = val;
  if (get_value("filename", val)) filename = val;
  if (get_value("fileomega", val))	fileomega = val;
  if (get_value("debug",val))	  debug = atoi(val.c_str()) ? true : false;
  if (get_value("usetag", val))   usetag = atoi(val.c_str());
  if (get_value("usebar", val))   
    {
      usebar = atoi(val.c_str()) ? true  : false;
      useorb = atoi(val.c_str()) ? false : true;
    }
  if (get_value("useorb", val))   
    {
      useorb = atoi(val.c_str()) ? true  : false;
      usebar = atoi(val.c_str()) ? false : true;
    }
}

double UserResPotN::get_omega(double t)
{
  if (t<Time.front()) return Omega.front();
  if (t>Time.back())  return Omega.back();

  return odd2(t, Time, Omega, 0);
}


void UserResPotN::determine_acceleration_and_potential(void)
{

  if (first) {

    if (restart) {

      if (myid == 0) {
				// Backup up old file
	string backupfile = outdir + filename + ".bak";
	string command("cp ");
	command += outdir + filename + " " + backupfile;
	system(command.c_str());
	
				// Open new output stream for writing
	ofstream out(string(outdir+filename).c_str());
	if (!out) {
	  cout << "UserResPotN: error opening new log file <" 
	       << filename << "> for writing\n";
	  MPI_Abort(MPI_COMM_WORLD, 121);
	  exit(0);
	}
	
				// Open old file for reading
	ifstream in(backupfile.c_str());
	if (!in) {
	  cout << "UserResPotN: error opening original log file <" 
	       << backupfile << "> for reading\n";
	  MPI_Abort(MPI_COMM_WORLD, 122);
	  exit(0);
	}

	const int linesize = 1024;
	char line[linesize];
	
	in.getline(line, linesize); // Discard header
	in.getline(line, linesize); // Next line

	double phase1, omlast1, tlast1;
	bool firstline = true;

	while (in) {

	  if (line[0] != '#' && line[0] != '!') {

	    istringstream ins(line);

	    ins >> tlast1;
	    ins >> phase1;
	    ins >> omlast1;

	    if (tlast1 >= tnow) {
	      if (firstline) {
		cerr << "UserResPotN: can't read log file, aborting" << endl;
		cerr << "UserResPotN: line=" << line << endl;
		MPI_Abort(MPI_COMM_WORLD, 123);
	      }
	      break;
	    }

	    firstline = false;
	    tlast = tlast1;
	    phase = phase1;
	    omlast = omlast1;

	  }

	  out << line << "\n";
	  
	  in.getline(line, linesize); // Next line
	}
				// Trapezoidal rule step
	phase += (tnow - tlast)*0.5*(omega + omlast);
      }

      MPI_Bcast(&tlast, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&phase, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast(&omega, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    }

    if (!restart) {

      if (myid==0) {		// Write header
	ofstream out(string(outdir+filename).c_str(), ios::out | ios::app);
	out.setf(ios::left);
	out << setw(15) << "# Time"
	    << setw(15) << "Phase"
	    << setw(15) << "Omega"
	    << setw(15) << "dOmega(tot)"
	    << setw(15) << "Bounds(tot)";
	for (int ir=0; ir<numRes; ir++) {
	  ostringstream olab;
	  olab << "dOmega(" << L1[ir] << "," << L2[ir] << ")";
	  out << setw(15) << olab.str().c_str();
	}
	for (int j=1; j<ResPot::NumDesc; j++)
	  out << setw(15) << ResPot::ReturnDesc[j];
	out << endl;

	char c = out.fill('-');
	int ncnt=1;
	out << "# " << setw(13) << ncnt++;
	out << "| " << setw(13) << ncnt++;
	out << "| " << setw(13) << ncnt++;
	out << "| " << setw(13) << ncnt++;
	out << "| " << setw(13) << ncnt++;
	for (int ir=0; ir<numRes; ir++)
	  out << "| " << setw(13) << ncnt++;
      	for (int j=1; j<ResPot::NumDesc; j++)
	  out << "| " << setw(13) << ncnt++;
	out << endl;
	out.fill(c);
      }
      
      phase = phase0;	// Initial phase 
    }
  } else {
				// Otherwise, do next for every time 
				// except the first
				// ----------------------------------------

				// Trapezoidal rule integration
    phase += (tnow - tlast)*0.5*(omega + omlast);
  }

				// Store current state
  tlast = tnow;
  omlast = omega;

				// Clear bounds counter
  for (int n=0; n<nthrds; n++) {
    for (int j=0; j<ResPot::NumDesc-1; j++) bcount[n][j] = 0;
  }

				// Clear difLz array
  for (int n=0; n<nthrds; n++) {
    for (int j=0; j<numRes; j++) difLz[n][j] = 0.0;
  }
  for (int j=0; j<numRes; j++) difLz0[j] = 0.0;

  // -----------------------------------------------------------
  // Compute the mapping
  // -----------------------------------------------------------

  exp_thread_fork(false);

  // -----------------------------------------------------------

				// Get total number out of bounds
  for (int j=0; j<ResPot::NumDesc-1; j++) {
    for (int n=1; n<nthrds; n++)  bcount[0][j] += bcount[n][j];
    btotn[j] = 0;
  }
  MPI_Reduce(&(bcount[0][0]), &btotn[0], ResPot::NumDesc-1, 
	     MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

				// Get total change in angular momentum
  for (int ir=0; ir<numRes; ir++) {
    for (int n=1; n<nthrds; n++) difLz[0][ir] += difLz[n][ir];
  }
  MPI_Allreduce(&difLz[0][0], &difLz0[0], numRes, 
		MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);


  double difLzT = 0.0;
  for (int ir=0; ir<numRes; ir++) difLzT += difLz0[ir];

  if (self)
    omega -= difLzT/Iz;
  else if (fileomega.size())
    omega = get_omega(tnow);
  else {
    if (dtom>0.0)
      omega = omega0*(1.0 + domega*0.5*(1.0 + erf( (tnow - tom0)/dtom )));
    else
      omega = omega0*(1.0 + domega*(tnow - tom0*0.5));
  }

				// Write diagnostic log
  if (myid==0) {
    int btot=0;
    for (int j=0; j<ResPot::NumDesc-1; j++) btot += btotn[j];
    ofstream out(string(outdir+filename).c_str(), ios::out | ios::app);
    out.setf(ios::left);
    out << setw(15) << tnow
	<< setw(15) << phase
	<< setw(15) << omega
	<< setw(15) << -difLzT/Iz
	<< setw(15) << btot;
    for (int ir=0; ir<numRes; ir++) out << setw(15) << -difLz0[ir]/Iz;
    for (int j=0; j<ResPot::NumDesc-1; j++)
      out << setw(15) << btotn[j];
    out << endl;
  }

  first = false;

  print_timings("UserResPotN: acceleration timings");
}


void * UserResPotN::determine_acceleration_and_potential_thread(void * arg) 
{
  double amp, R0, R1;
  double posI[3], posO[3], velI[3], velO[3], vdif[3], Lz0, Lz1;
  
  unsigned nbodies = cC->Number();
  int id = *((int*)arg);
  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;

  thread_timing_beg(id);

  amp = AMP *
    0.5*(1.0 + erf( (tnow - ton) /delta )) *
    0.5*(1.0 + erf( (toff - tnow)/delta )) ;
    
  
  vector<double> Phase(3);
  Phase[0] = phase;
  Phase[1] = phase + omega*0.5*dtime;
  Phase[2] = phase + omega*dtime;

				// Check for nan (can get rid of this
				// eventually)
  bool updated;
  bool found_nan = false;
  ResPot::ReturnCode ret;
  double dpot;
  int ir;

  map<unsigned long, Particle>::iterator it = cC->Particles().begin();
  unsigned long i;

  for (int q=0   ; q<nbeg; q++) it++;
  for (int q=nbeg; q<nend; q++) {
    i = (it++)->first;
				// If we are multistepping, compute accel 
				// only at or below this level

    if (multistep && (cC->Part(i)->level < mlevel)) continue;

    ret = ResPot::OK;		// Reset error flags
    updated = false;

    if (usetag>=0 && cC->Part(i)->iattrib[usetag]) continue;

				// Initial conditions
    R0 = 0.0;
    for (int k=0; k<3; k++) {
      posI[k] = cC->Pos(i, k);
      if (c0) posI[k] -= c0->com[k];
      velI[k] = cC->Vel(i, k);

      R0 += posI[k]*posI[k];
    }
    R0 = sqrt(R0);
    
				// Initial Lz
    Lz0 = posI[0]*velI[1] - posI[1]*velI[0];
    
    ir = i % numRes;
      
    if ((ret=respot[ir]-> 
	 Update(dtime, Phase, amp, posI, velI, posO, velO)) == ResPot::OK) {
	
				// Apply two-body diffusion
      if (pmass>0.0) {
	diffuse->get_diffusion(dtime, posO, velO, vdif);
	for (int k=0; k<3; k++) velO[k] += vdif[k];
      }
				// Current ang mom
      Lz1 = posO[0]*velO[1] - posO[1]*velO[0];

				// Accumulate change in Lz for each resonance
      if (respot[ir]->K()<Klim)
	difLz[id][ir] += cC->Mass(i)*(Lz1 - Lz0) * numRes;
	
      updated = true;

    } else {

      bcount[id][ret-1]++;

      if (usetag>=0) cC->Part(i)->iattrib[usetag] = 1;
#ifdef DEBUG
      pthread_mutex_lock(&iolock);
      cout << "Process " << myid << " id=" << id << ":"
	   << " i=" << myid << " Error=" << ResPot::ReturnDesc[ret] << endl;
      pthread_mutex_unlock(&iolock);
#endif
    }



    if (!updated) {
				// Try zero amplitude update
      if ((ret=respot[ir]-> 
	   Update(dtime, Phase, 0.0, posI, velI, posO, velO)) != ResPot::OK) {
	
	dpot = halo_model->get_dpot(R0);
	for (int k=0; k<3; k++) {
	  posO[k] = posI[k] + velI[k] * dtime;
	  velO[k] = velI[k] - dpot*posI[k]/R0 * dtime;
	}
      }
    }

    R1 = 0.0;
    for (int k=0; k<3; k++) {
      cC->Part(i)->pos[k] = posO[k];
      cC->Part(i)->vel[k] = velO[k];
      cC->Part(i)->acc[k] = (velO[k] - velI[k])/dtime;
      if (!found_nan) {
	if ( isnan(cC->Pos(i, k)) ||
	     isnan(cC->Vel(i, k)) ||
	     isnan(cC->Acc(i, k)) ) found_nan = true; 
      }
      R1 += posO[k]*posO[k];
    }
    R1 = sqrt(R1);

    cC->Part(i)->potext = halo_model->get_pot(R1);
    
    if (found_nan) {
      cout << "Process " << myid << ": found nan\n";
      for (int k=0; k<3; k++) cout << setw(15) << cC->Pos(i, k);
      for (int k=0; k<3; k++) cout << setw(15) << cC->Vel(i, k);
      for (int k=0; k<3; k++) cout << setw(15) << cC->Acc(i, k);
      cout << endl << flush;
      found_nan = false;
    }
    
#if 1
    if (i==10) {
      ostringstream sout;
      sout << outdir << "test_orbit.respot." << myid;
      ofstream out(sout.str().c_str(), ios::app | ios::out);
      if (out) {
	out << setw(15) << tnow;
	for (int k=0; k<3; k++) out << setw(15) << posI[k];
	double v2 = 0.0;
	for (int k=0; k<3; k++) {
	  out << setw(15) << velI[k];
	  v2 += velI[k]*velI[k];
	}
	out << setw(15) << 0.5*v2 + halo_model->get_pot(R0);
	
	for (int k=0; k<3; k++) out << setw(15) << posO[k];
	v2 = 0.0;
	for (int k=0; k<3; k++) {
	  out << setw(15) << velO[k];
	  v2 += velO[k]*velO[k];
	}
	out << setw(15) << 0.5*v2 + halo_model->get_pot(R1) << endl;
      }
    }
#endif

  }

  thread_timing_end(id);

  return (NULL);
}


extern "C" {
  ExternalForce *makerResPotN(string& line)
  {
    return new UserResPotN(line);
  }
}

class proxyN { 
public:
  proxyN()
  {
    factory["userrespot2"] = makerResPotN;
  }
};

static proxyN p;
