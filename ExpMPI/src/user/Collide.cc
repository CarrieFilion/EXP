#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

using namespace std;

#include "Timer.h"
#include "global.H"
#include "pHOT.H"
#include "UserTreeDSMC.H"
#include "Collide.H"

				// Use the original Pullin velocity 
				// selection algorithm
bool Collide::PULLIN = false;
				// Print out sorted cell parameters
bool Collide::SORTED = false;
				// Print out T-rho plane for cells 
				// with mass weighting
bool Collide::PHASE = false;
				// Extra debugging output
bool Collide::EXTRA = false;
				// Turn off collisions for testing
bool Collide::DRYRUN = false;
				// Turn off cooling for testing
bool Collide::NOCOOL = false;
				// Temperature floor in EPSM
double Collide::TFLOOR = 1000.0;
				// Proton mass (g)
const double mp = 1.67262158e-24;
				// Boltzmann constant (cgs)
const double boltz = 1.3810e-16;

extern "C"
void *
collide_thread_call(void *atp)
{
  thrd_pass_Collide *tp = (thrd_pass_Collide *)atp;
  Collide *p = (Collide *)tp->p;
  p -> collide_thread((void*)&tp->arg);
  return NULL;
}

void Collide::collide_thread_fork(pHOT* tree, double Fn, double tau)
{
  int errcode;
  void *retval;
  
  if (nthrds==1) {
    thrd_pass_Collide td;
    
    td.p = this;
    td.arg.tree = tree;
    td.arg.fn = Fn;
    td.arg.tau = tau;
    td.arg.id = 0;

    collide_thread_call(&td);

    return;
  }

  td = new thrd_pass_Collide [nthrds];
  t = new pthread_t [nthrds];

  if (!td) {
    cerr << "Process " << myid 
         << ": collide_thread_fork: error allocating memory for thread counters\n";
    exit(18);
  }
  if (!t) {
    cerr << "Process " << myid
         << ": collide_thread_fork: error allocating memory for thread\n";
    exit(18);
  }

                                // Make the <nthrds> threads
  for (int i=0; i<nthrds; i++) {
    td[i].p = this;
    td[i].arg.tree = tree;
    td[i].arg.fn = Fn;
    td[i].arg.tau = tau;
    td[i].arg.id = i;

    errcode =  pthread_create(&t[i], 0, collide_thread_call, &td[i]);
    if (errcode) {
      cerr << "Process " << myid;
      cerr << " collide: cannot make thread " << i
	   << ", errcode=" << errcode << endl;
      exit(19);
    }
  }
    
  waitTime.start();

                                // Collapse the threads
  for (int i=0; i<nthrds; i++) {
    if ((errcode=pthread_join(t[i], &retval))) {
      cerr << "Process " << myid;
      cerr << " collide: thread join " << i
           << " failed, errcode=" << errcode << endl;
      exit(20);
    }
    if (i==0) {
      waitSoFar = waitTime.stop();
      joinTime.start();
    }
  }
  
  joinSoFar = joinTime.stop();

  delete [] td;
  delete [] t;
}


int Collide::CNUM = 0;
bool Collide::CBA = true;
double Collide::EPSMratio = -1.0;

Collide::Collide(double diameter, int nth)
{
  nthrds = nth;

  colcntT = vector< vector<unsigned> > (nthrds);
  numcntT = vector< vector<unsigned> > (nthrds);
  error1T = vector<unsigned> (nthrds, 0);
  col1T = vector<unsigned> (nthrds, 0);
  epsm1T = vector<unsigned> (nthrds, 0);
  Nepsm1T = vector<unsigned> (nthrds, 0);
  tmassT = vector<double> (nthrds, 0);
  decelT = vector<double> (nthrds, 0);

  tsratT  = vector< vector<double> > (nthrds);
  deratT  = vector< vector<double> > (nthrds);
  tdensT  = vector< vector<double> > (nthrds);
  tvolcT  = vector< vector<double> > (nthrds);
  ttempT  = vector< vector<double> > (nthrds);
  tdeltT  = vector< vector<double> > (nthrds);
  tdispT  = vector< vector<double> > (nthrds);
  tselnT  = vector< vector<double> > (nthrds);
  tphaseT = vector< vector<Precord> > (nthrds);
  tmfpstT = vector< vector<Precord> > (nthrds);

  cellist = vector< vector<pCell*> > (nthrds);

  diam0 = diam = diameter;
  coltot = 0;			// Count total collisions
  errtot = 0;			// Count errors in inelastic computation
  epsmcells = 0;		// Count cells in EPSM regime
  epsmtot = 0;			// Count particles in EPSM regime

				// Default cooling rate (if not set by derived class)
  coolrate = vector<double>(nthrds, 0.0);

				// EPSM diagnostics
  lostSoFar_EPSM = vector<double>(nthrds, 0.0);

  snglTime.Microseconds();
  forkTime.Microseconds();
  waitTime.Microseconds();
  joinTime.Microseconds();

  collTime = vector<Timer>(nthrds);
  collSoFar = vector<TimeElapsed>(nthrds);
  collCnt = vector<int>(nthrds, 0);
  for (int n=0; n<nthrds; n++) collTime[n].Microseconds();
  
  tdiag  = vector<unsigned>(numdiag, 0);
  tdiag1 = vector<unsigned>(numdiag, 0);
  tdiag0 = vector<unsigned>(numdiag, 0);
  tdiagT = vector< vector<unsigned> > (nthrds);

  tcool  = vector<unsigned>(numdiag, 0);
  tcool1 = vector<unsigned>(numdiag, 0);
  tcool0 = vector<unsigned>(numdiag, 0);
  tcoolT = vector< vector<unsigned> > (nthrds);

  for (int n=0; n<nthrds; n++) {
    tdiagT[n] = vector<unsigned>(numdiag, 0);
    tcoolT[n] = vector<unsigned>(numdiag, 0);
    tdispT[n] = vector<double>(3, 0);
  }

  disptot = vector<double>(3, 0);
  masstot = 0.0;

  use_temp = -1;
  use_dens = -1;
  use_delt = -1;
  use_exes = -1;

  gen = new ACG(11+myid);
  unit = new Uniform(0.0, 1.0, gen);
  norm = new Normal(0.0, 1.0, gen);

  prec = vector<Precord>(nthrds);
  for (int n=0; n<nthrds; n++)
    prec[n].second = vector<double>(Nmfp, 0);
}

Collide::~Collide()
{
  delete gen;
  delete unit;
  delete norm;
}

void Collide::debug_list(pHOT& tree)
{
  unsigned ncells = tree.Number();
  pHOT_iterator c(tree);
  for (int cid=0; cid<numprocs; cid++) {
    if (myid == cid) {
      ostringstream sout;
      sout << "==== Collide " << myid << " ncells=" << ncells;
      cout << setw(70) << setfill('=') << left << sout.str() 
	   << endl << setfill(' ');

      for (int n=0; n<nthrds; n++) {
	int nbeg = ncells*(n  )/nthrds;
	int nend = ncells*(n+1)/nthrds;
	for (int j=nbeg; j<nend; j++) {
	  int tnum = c.nextCell();
	  cout << setw(8)  << j
	       << setw(12) << c.Cell()
	       << setw(12) << cellist[n][j-nbeg]
	       << setw(12) << c.Cell()->bods.size()
	       << setw(12) << tnum << endl;
	}
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
}


unsigned Collide::collide(pHOT& tree, double Fn, double tau, int mlevel)
{
  snglTime.start();

				// Clean thread variables
  for (int n=0; n<nthrds; n++) {
    error1T[n] = 0;
    col1T[n] = 0;
    epsm1T[n] = 0;
    Nepsm1T[n] = 0;
    tmassT[n] = 0;
    decelT[n] = 0;
				// For computing cell occupation #
    colcntT[n].clear();		// and collision counts
    numcntT[n].clear();
				// For computing MFP to cell size ratio 
				// and drift ratio
    tsratT[n].clear();
    deratT[n].clear();
    tdensT[n].clear();
    tvolcT[n].clear();
    ttempT[n].clear();
    tdeltT[n].clear();
    tselnT[n].clear();
    tphaseT[n].clear();
    tmfpstT[n].clear();

    for (unsigned k=0; k<numdiag; k++) {
      tdiagT[n][k] = 0;
      if (use_delt>=0) tcoolT[n][k] = 0;
    }

    for (unsigned k=0; k<3; k++) tdispT[n][k] = 0;
  }
  for (unsigned k=0; k<numdiag; k++) tdiag1[k] = tdiag0[k] = 0;
  if (use_delt>=0) 
    for (unsigned k=0; k<numdiag; k++) tcool1[k] = tcool0[k] = 0;

  // DEBUG
  list_sizes();

				// Make cellist
  for (int n=0; n<nthrds; n++) cellist[n].clear();
  unsigned ncells = 0;
  list<pCell*>::iterator ic;
  for (unsigned M=mlevel; M<=multistep; M++) {
    if (tree.clevels[M].size())	// Don't queue null cells
      for (ic=tree.clevels[M].begin(); ic!=tree.clevels[M].end(); ic++) {
	cellist[(ncells++)%nthrds].push_back(*ic);
	/*
	if (static_cast<pCell*>(*ic)->bods.size() == 0)
	  cout << "Collide: M=" << M << ", tot=" << tree.clevels[M].size()
	       << ", ptr=" << hex << *ic << dec
	       << ", bods=" << (*ic)->bods.size()
	       << ", cells so far=" << ncells
	       << ", loading a null cell!" << endl;
	*/
      }
  }
      
#ifdef DEBUG
  debug_list(tree);
#endif

  snglTime.stop();
  forkTime.start();
  {
    ostringstream sout;
    sout << "before fork, " << __FILE__ << ": " << __LINE__;
    tree.checkBounds(2.0, sout.str().c_str());
  }
  collide_thread_fork(&tree, Fn, tau);
  {
    ostringstream sout;
    sout << "after fork, " << __FILE__ << ": " << __LINE__;
    tree.checkBounds(2.0, sout.str().c_str());
  }
  forkSoFar = forkTime.stop();
  snglTime.start();

				// Diagnostics
  unsigned error1=0, error=0;

  unsigned col1=0, col=0;	// Count number of collisions
  unsigned epsm1=0, epsm=0, Nepsm1=0, Nepsm=0;

				// Dispersion test
  double mass1 = 0, mass0 = 0;
  vector<double> disp1(3, 0), disp0(3, 0);

  numcnt.clear();
  colcnt.clear();

  for (int n=0; n<nthrds; n++) {
    error1 += error1T[n];
    col1 += col1T[n];
    epsm1 += epsm1T[n];
    Nepsm1 += Nepsm1T[n];
    numcnt.insert(numcnt.end(), numcntT[n].begin(), numcntT[n].end());
    colcnt.insert(colcnt.end(), colcntT[n].begin(), colcntT[n].end());
    for (unsigned k=0; k<numdiag; k++) tdiag1[k] += tdiagT[n][k];
    if (use_delt>=0) 
      for (unsigned k=0; k<numdiag; k++) tcool1[k] += tcoolT[n][k];
  }

				// For computing MFP to cell size ratio 
				// and drift ratio (diagnostic only)
  tsrat.clear();
  derat.clear();
  tdens.clear();
  tvolc.clear();
  ttemp.clear();
  tdelt.clear();
  tseln.clear();
  tphase.clear();
  tmfpst.clear();

  for (int n=0; n<nthrds; n++) {
    tsrat. insert(tsrat.end(),   tsratT[n].begin(),  tsratT[n].end());
    derat. insert(derat.end(),   deratT[n].begin(),  deratT[n].end());
    tdens. insert(tdens.end(),   tdensT[n].begin(),  tdensT[n].end());
    tvolc. insert(tvolc.end(),   tvolcT[n].begin(),  tvolcT[n].end());
    ttemp. insert(ttemp.end(),   ttempT[n].begin(),  ttempT[n].end());
    tdelt. insert(tdelt.end(),   tdeltT[n].begin(),  tdeltT[n].end());
    tseln. insert(tseln.end(),   tselnT[n].begin(),  tselnT[n].end());
    tphase.insert(tphase.end(), tphaseT[n].begin(), tphaseT[n].end());
    tmfpst.insert(tmfpst.end(), tmfpstT[n].begin(), tmfpstT[n].end());

    for (unsigned k=0; k<3; k++) disp1[k] += tdispT[n][k];
    mass1 += tmassT[n];
  }

  MPI_Reduce(&col1, &col, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&epsm1, &epsm, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&Nepsm1, &Nepsm, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&error1, &error, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&ncells, &numtot, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&tdiag1[0], &tdiag0[0], numdiag, MPI_UNSIGNED, MPI_SUM, 0, 
	     MPI_COMM_WORLD);
  if (use_delt>=0)
    MPI_Reduce(&tcool1[0], &tcool0[0], numdiag, MPI_UNSIGNED, MPI_SUM, 0, 
	       MPI_COMM_WORLD);
  MPI_Reduce(&disp1[0], &disp0[0], 3, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&mass1, &mass0, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

  coltot += col;
  epsmtot += epsm;
  epsmcells += Nepsm;
  errtot += error;
  for (unsigned k=0; k<numdiag; k++) tdiag[k] += tdiag0[k];
  if (use_delt>=0)
    for (unsigned k=0; k<numdiag; k++) tcool[k] += tcool0[k];
  for (unsigned k=0; k<3; k++) disptot[k] += disp0[k];
  masstot += mass0;

  snglSoFar = snglTime.stop();

  return( col );
}

void Collide::dispersion(vector<double>& disp)
{
  disp = disptot;
  if (masstot>0.0) {
    for (unsigned k=0; k<3; k++) disp[k] /= masstot;
  }
  for (unsigned k=0; k<3; k++) disptot[k] = 0.0;
  masstot = 0.0;
}


void * Collide::collide_thread(void * arg)
{
  pHOT *tree = (pHOT*)((thrd_pass_arguments*)arg)->tree;
  double Fn = (double)((thrd_pass_arguments*)arg)->fn;
  double tau = (double)((thrd_pass_arguments*)arg)->tau;
  int id = (int)((thrd_pass_arguments*)arg)->id;

				// Work vectors
  vector<double> vcm(3), vrel(3), crel(3);
  pCell *c;

  // Loop over cells, processing collisions in each cell
  //
  for (unsigned j=0; j<cellist[id].size(); j++ ) {

    // Number of particles in this cell
    //
    c = cellist[id][j];
    unsigned number = c->bods.size();
    numcntT[id].push_back(number);

    // Skip cells with only one particle
    //
    if( number < 2 ) {
      colcntT[id].push_back(0);
      continue;  // Skip to the next cell
    }

    // Energy lost in this cell
    //
    decelT[id] = 0.0;

    // Compute 1.5 times the mean relative velocity in each MACRO cell
    //
    double crm = 0;
    pCell *samp = c->sample;
    if (samp->state[0]>0.0) {
      for (unsigned k=0; k<3; k++) 
	crm += (samp->state[1+k] - 
		samp->state[4+k]*samp->state[4+k]/samp->state[0])/samp->state[0];
    }
    crm = 1.5*sqrt(2.0*fabs(crm));

    // KE in the cell
    //
    double kedsp=0.0;
    if (c->state[0]>0.0) {
      for (unsigned k=0; k<3; k++) 
	kedsp += 0.5*(c->state[1+k] - c->state[4+k]*c->state[4+k]/c->state[0]);
    }
    
    // Volume in the cell
    //
    double volc = c->Volume();

    // Mass in the cell
    //
    double mass = c->Mass();

    // Fiducial cross section
    //
    diam = diam0;
    double cross  = M_PI*diam*diam;

    // Determine cross section based on fixed number of collisions
    //
    if (CNUM) {
      cross = 2.0*CNUM*volc/(Fn*mass*tau*crm*number*(number-1));
      diam = sqrt(cross/M_PI);
    }

    double diamCBA = sqrt(Fn*mass)*diam;

    // Diagnostic: MFP to linear cell size ratio 
    //
    tsratT[id].push_back(crm/1.5*tau/pow(volc,0.33333333));
    tdensT[id].push_back(number/volc);
    tvolcT[id].push_back(volc);
    
    double posx, posy, posz;
    c->MeanPos(posx, posy, posz);
    
    // MFP = 1/(n*cross_section)
    // MFP/side = MFP/vol^(1/3) = vol^(2/3)/(number*cross_section)

    prec[id].first = pow(volc, 0.66666667)/(Fn*mass*cross*number);
    prec[id].second[0] = sqrt(posx*posx + posy*posy);
    prec[id].second[1] = posz;
    prec[id].second[2] = sqrt(posx*posx+posy*posy*+posz*posz);
    prec[id].second[3] = mass/volc;
    prec[id].second[4] = volc;

    tmfpstT[id].push_back(prec[id]);
    
    // Determine number of candidate collision pairs
    // to be selected in this cell
    //
    double coeff  = 0.5*number*(number-1)*Fn*mass/volc*cross*tau;
    double select = coeff*crm;

				// Diagnose time step in this cell
    double vmass;
    vector<double> V1, V2;
    c->Vel(vmass, V1, V2);
    double scale = c->Scale();
    double taudiag = 1.0e40;
    for (int k=0; k<3; k++) {
      taudiag = min<double>
	(pHOT::sides[k]*scale/(sqrt(V2[k]/vmass)+1.0e-40), taudiag);
    }

    int indx = (int)floor(log(taudiag/tau)/log(4.0) + 5);
    if (indx<0 ) indx = 0;
    if (indx>10) indx = 10;
    tdiagT[id][indx]++;
    
				// Number per selection ratio
    tselnT[id].push_back(static_cast<double>(number)/select);


    double length = pow(c->Volume(), 0.3333333);

				// Number of pairs to be selected
    unsigned nsel = (int)floor(select+0.5);
    
    collTime[id].start();
    initialize_cell(c, crm, tau, select, id);
    collCnt[id]++;
    
				// No collisions, primarily for testing . . .
    if (DRYRUN) continue;

				// If more than EPSMratio collisions per
				// particle, assume equipartition
    if (static_cast<double>(number)/select < EPSMratio) {

      EPSM(tree, c, id);

    } else {

      unsigned colc = 0;

      // Loop over total number of candidate collision pairs
      //
      for (unsigned i=0; i<nsel; i++ ) {

	// Pick two particles at random out of this cell
	//
	unsigned k1 = min<int>((int)floor((*unit)()*number), number-1);
	unsigned k2 = ((int)floor((*unit)()*(number-1)) + k1 + 1) % number;
	Particle* p1 = tree->Body(c->bods[k1]); // First particle
	Particle* p2 = tree->Body(c->bods[k2]); // Second particle
	
	// Calculate pair's relative speed (pre-collision)
	//
	double cr = 0.0;
	for (int k=0; k<3; k++) {
	  crel[k] = p1->vel[k] - p2->vel[k];
	  cr += crel[k]*crel[k];
	}
	cr = sqrt(cr);
	
	if( cr > crm )         // If relative speed larger than crm,
	  crm = cr;            // then reset crm to larger value

	// Accept or reject candidate pair according to relative speed
	//
	if( cr/crm > (*unit)() ) {
	  // If pair accepted, select post-collision velocities
	  //
	  colc++;			// Collision counter

				// Do inelastic stuff
	  error1T[id] += inelastic(tree, p1, p2, &cr, id);
	  // May update relative velocity to reflect
	  // excitation of internal degrees of freedom
	  
	  // Center of mass velocity
	  double tmass = p1->mass + p2->mass;
	  for(unsigned k=0; k<3; k++)
	    vcm[k] = (p1->mass*p1->vel[k] + p2->mass*p2->vel[k]) / tmass;


	  double cos_th = 1.0 - 2.0*(*unit)();       // Cosine and sine of
	  double sin_th = sqrt(1.0 - cos_th*cos_th); // collision angle theta
	  double phi = 2.0*M_PI*(*unit)();           // Collision angle phi

	  vrel[0] = cr*cos_th;             // Compute post-collision
	  vrel[1] = cr*sin_th*cos(phi);    // relative velocity
	  vrel[2] = cr*sin_th*sin(phi);

				// Update post-collision velocities
				// 
	  for(unsigned k=0; k<3; k++ ) {
	    p1->vel[k] = vcm[k] + p2->mass/tmass*vrel[k];
	    p2->vel[k] = vcm[k] - p1->mass/tmass*vrel[k];
	  }

	  if (CBA) {

	    // Calculate pair's relative speed (post-collision)
	    //
	    cr = 0.0;
	    for (int k=0; k<3; k++) {
	      crel[k] = p1->vel[k] - p2->vel[k] - crel[k];
	      cr += crel[k]*crel[k];
	    }
	    cr = sqrt(cr);
	    
	    // Displacement
	    //
	    if (cr>0.0) {
	      double displ;
	      for (int k=0; k<3; k++) {
		displ = crel[k]*diamCBA/cr;
		if (displ > 0.5*length) {
		  cout << endl << "Huge displacement in CBA, process " << myid << endl
		       << "   id=" << id << ": displ=" << displ << endl
		       << "   len=" << length << " diam=" << diamCBA << endl;
		}
		p1->pos[k] += displ;
		p2->pos[k] -= displ;
	      }
	    }
	  }
	  
	} // Loop over pairs

      }

      // Count collisions
      //
      colcntT[id].push_back(colc);
      col1T[id] += colc;

    }
    collSoFar[id] = collTime[id].stop();

    // Compute dispersion diagnostics
    //
    double tmass = 0.0;
    vector<double> velm(3, 0.0), velm2(3, 0.0);
    for (unsigned j=0; j<number; j++) {
      Particle* p = tree->Body(c->bods[j]);
      for (unsigned k=0; k<3; k++) {
	velm[k]  += p->mass*p->vel[k];
	velm2[k] += p->mass*p->vel[k]*p->vel[k];
      }
      tmass += p->mass;
    }

    if (tmass>0.0) {
      for (unsigned k=0; k<3; k++) {
	velm[k] /= tmass;
	velm2[k] = velm2[k] - velm[k]*velm[k]*tmass;
	if (velm2[k]>0.0) {
	  tdispT[id][k] += velm2[k];
	  tmassT[id]    += tmass;
	}
      }
    }
    
    // Energy lost from this cell compared to target
    //
    if (coolrate[id]>0.0) {
      if (mass>0.0) {
	if (kedsp>0.0) 
	  deratT[id].push_back( (decelT[id] - coolrate[id])/kedsp );
	if (use_exes>=0) {
	  double dE = (decelT[id] - coolrate[id])/mass;
	  for (unsigned j=0; j<number; j++) {
	    Particle* p = tree->Body(c->bods[j]);
	    if (use_exes<static_cast<int>(p->dattrib.size())) {
	      p->dattrib[use_exes] += dE*p->mass;
	    }
	  }
	}
      }
    }

  } // Loop over cells

  return (NULL);
}


unsigned Collide::medianNumber() 
{
  MPI_Status s;

  if (myid==0) {
    unsigned num;
    for (int n=1; n<numprocs; n++) {
      MPI_Recv(&num, 1, MPI_UNSIGNED, n, 39, MPI_COMM_WORLD, &s);
      vector<unsigned> tmp(num);
      MPI_Recv(&tmp[0], num, MPI_UNSIGNED, n, 40, MPI_COMM_WORLD, &s);
      numcnt.insert(numcnt.end(), tmp.begin(), tmp.end());
    }

    std::sort(numcnt.begin(), numcnt.end()); 

    if (EXTRA) {
      ofstream out("tmp.numcnt");
      for (unsigned j=0; j<numcnt.size(); j++)
	out << setw(8) << j << setw(18) << numcnt[j] << endl;
    }

    return numcnt[numcnt.size()/2]; 

  } else {
    unsigned num = numcnt.size();
    MPI_Send(&num, 1, MPI_UNSIGNED, 0, 39, MPI_COMM_WORLD);
    MPI_Send(&numcnt[0], num, MPI_UNSIGNED, 0, 40, MPI_COMM_WORLD);

    return 0;
  }
}

unsigned Collide::medianColl() 
{ 
  MPI_Status s;

  if (myid==0) {
    unsigned num;
    for (int n=1; n<numprocs; n++) {
      MPI_Recv(&num, 1, MPI_UNSIGNED, n, 39, MPI_COMM_WORLD, &s);
      vector<unsigned> tmp(num);
      MPI_Recv(&tmp[0], num, MPI_UNSIGNED, n, 40, MPI_COMM_WORLD, &s);
      colcnt.insert(colcnt.end(), tmp.begin(), tmp.end());
    }

    std::sort(colcnt.begin(), colcnt.end()); 

    if (EXTRA) {
      ostringstream ostr;
      ostr << runtag << ".colcnt";
      ofstream out(ostr.str().c_str());
      for (unsigned j=0; j<colcnt.size(); j++)
	out << setw(8) << j << setw(18) << colcnt[j] << endl;
    }

    return colcnt[colcnt.size()/2]; 

  } else {
    unsigned num = colcnt.size();
    MPI_Send(&num, 1, MPI_UNSIGNED, 0, 39, MPI_COMM_WORLD);
    MPI_Send(&colcnt[0], num, MPI_UNSIGNED, 0, 40, MPI_COMM_WORLD);
    return 0;
  }

}

void Collide::collQuantile(vector<double>& quantiles, vector<double>& coll_)
{
  MPI_Status s;

  if (myid==0) {
    unsigned num;
    for (int n=1; n<numprocs; n++) {
      MPI_Recv(&num, 1, MPI_UNSIGNED, n, 39, MPI_COMM_WORLD, &s);
      vector<unsigned> tmp(num);
      MPI_Recv(&tmp[0], num, MPI_UNSIGNED, n, 40, MPI_COMM_WORLD, &s);
      colcnt.insert(colcnt.end(), tmp.begin(), tmp.end());
    }

    std::sort(colcnt.begin(), colcnt.end()); 

    coll_ = vector<double>(quantiles.size());
    for (unsigned j=0; j<quantiles.size(); j++)
      coll_[j] = colcnt[(unsigned)floor(quantiles[j]*colcnt.size())];

    ostringstream ostr;
    ostr << runtag << ".colcnt";
    ofstream out(ostr.str().c_str());
    out << "Cell data:" << endl;
    for (unsigned j=0; j<colcnt.size(); j++)
      out << setw(8) << j << setw(18) << colcnt[j] << endl;
    out << endl << "Quantiles:" << endl;
    for (unsigned j=0; j<quantiles.size(); j++)
      out << setw(8) << quantiles[j] << setw(18) << coll_[j] << endl;
    
  } else {
    unsigned num = colcnt.size();
    MPI_Send(&num, 1, MPI_UNSIGNED, 0, 39, MPI_COMM_WORLD);
    MPI_Send(&colcnt[0], num, MPI_UNSIGNED, 0, 40, MPI_COMM_WORLD);
  }
}

void Collide::mfpsizeQuantile(vector<double>& quantiles, 
			      vector<double>& mfp_, 
			      vector<double>& ts_,
			      vector<double>& coll_,
			      vector<double>& rate_) 
{
  MPI_Status s;

  if (myid==0) {
    unsigned nmb, num;
    for (int n=1; n<numprocs; n++) {
      MPI_Recv(&nmb, 1, MPI_UNSIGNED, n, 38, MPI_COMM_WORLD, &s);
      MPI_Recv(&num, 1, MPI_UNSIGNED, n, 39, MPI_COMM_WORLD, &s);
      vector<double> tmb(nmb), tmp(num);
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 40, MPI_COMM_WORLD, &s);
      tsrat.insert(tsrat.end(), tmp.begin(), tmp.end());
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 41, MPI_COMM_WORLD, &s);
      tdens.insert(tdens.end(), tmp.begin(), tmp.end());
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 42, MPI_COMM_WORLD, &s);
      tvolc.insert(tvolc.end(), tmp.begin(), tmp.end());
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 43, MPI_COMM_WORLD, &s);
      ttemp.insert(ttemp.end(), tmp.begin(), tmp.end());
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 44, MPI_COMM_WORLD, &s);
      tdelt.insert(tdelt.end(), tmp.begin(), tmp.end());
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 45, MPI_COMM_WORLD, &s);
      tseln.insert(tseln.end(), tmp.begin(), tmp.end());
      MPI_Recv(&tmb[0], nmb, MPI_DOUBLE, n, 46, MPI_COMM_WORLD, &s);
      derat.insert(derat.end(), tmb.begin(), tmb.end());

      vector<Precord> tmp2(num);

      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 47, MPI_COMM_WORLD, &s);
      for (unsigned k=0; k<num; k++) {
				// Load density
	tmp2[k].first = tmp[k];
				// Initialize record
	tmp2[k].second = vector<double>(Nphase, 0);
      }
      for (unsigned l=0; l<Nphase; l++) {
	MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 48+l, MPI_COMM_WORLD, &s);
	for (unsigned k=0; k<num; k++) tmp2[k].second[l] = tmp[k];
      }
      tphase.insert(tphase.end(), tmp2.begin(), tmp2.end());

      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 48+Nphase, MPI_COMM_WORLD, &s);
      for (unsigned k=0; k<num; k++) {
				// Load mfp
	tmp2[k].first = tmp[k];
				// Initialize record
	tmp2[k].second = vector<double>(Nmfp, 0);
      }
      for (unsigned l=0; l<Nmfp; l++) {
	MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 49+Nphase+l, MPI_COMM_WORLD, &s);
	for (unsigned k=0; k<num; k++) tmp2[k].second[l] = tmp[k];
      }
      tmfpst.insert(tmfpst.end(), tmp2.begin(), tmp2.end());
    }

    std::sort(tsrat.begin(),  tsrat.end()); 
    std::sort(tdens.begin(),  tdens.end()); 
    std::sort(tvolc.begin(),  tvolc.end()); 
    std::sort(ttemp.begin(),  ttemp.end()); 
    std::sort(tdelt.begin(),  tdelt.end()); 
    std::sort(tseln.begin(),  tseln.end()); 
    std::sort(tphase.begin(), tphase.end());
    std::sort(tmfpst.begin(), tmfpst.end());
    std::sort(derat.begin(),  derat.end());

    mfp_  = vector<double>(quantiles.size());
    ts_   = vector<double>(quantiles.size());
    coll_ = vector<double>(quantiles.size());
    rate_ = vector<double>(quantiles.size());
    for (unsigned j=0; j<quantiles.size(); j++) {
      if (tmfpst.size())
	mfp_[j]  = tmfpst[(unsigned)floor(quantiles[j]*tmfpst.size())].first;
      else
	mfp_[j] = 0;
      if (tsrat.size())
	ts_[j]   = tsrat [(unsigned)floor(quantiles[j]*tsrat.size()) ];
      else
	ts_[j]   = 0;
      if (tseln.size())
	coll_[j] = tseln [(unsigned)floor(quantiles[j]*tseln.size()) ];
      else
	coll_[j] = 0;
      if (derat.size())
	rate_[j] = derat [(unsigned)floor(quantiles[j]*derat.size()) ];
      else
	rate_[j] = 0;
    }

    if (SORTED) {
      ostringstream ostr;
      ostr << runtag << ".collide." << this_step;
      ofstream out(ostr.str().c_str());
      out << left << setw(8) << "# N" // Header
	  << setw(18) << "| MFP/L"
	  << setw(18) << "| Cyl radius (MFP)"
	  << setw(18) << "| Vertical (MFP)"
	  << setw(18) << "| Sph radius (MFP)"
	  << setw(18) << "| Density(MFP)"
	  << setw(18) << "| Volume(MFP)"
	  << setw(18) << "| TOF/TS"
	  << setw(18) << "| Density"
	  << setw(18) << "| Cell vol"
	  << setw(18) << "| Cell temp"
	  << setw(18) << "| Cool/part"
	  << setw(18) << "| Number/Nsel"
	  << endl;
      out << "# " << setw(6) << 1;
      for (unsigned k=2; k<13; k++) out << "| " << setw(16) << k;
      out << endl;
      for (unsigned j=0; j<tmfpst.size(); j++)
	out << setw(8) << j 
	    << setw(18) << tmfpst[j].first
	    << setw(18) << tmfpst[j].second[0]
	    << setw(18) << tmfpst[j].second[1]
	    << setw(18) << tmfpst[j].second[2]
	    << setw(18) << tmfpst[j].second[3]
	    << setw(18) << tmfpst[j].second[4]
	    << setw(18) << tsrat[j] 
	    << setw(18) << tdens[j] 
	    << setw(18) << tvolc[j] 
	    << setw(18) << ttemp[j] 
	    << setw(18) << tdelt[j] 
	    << setw(18) << tseln[j] 
	    << endl;
    }


    if (PHASE) {
      ostringstream ostr;
      ostr << runtag << ".phase." << this_step;
      ofstream out(ostr.str().c_str());
      out << left << setw(8) << "# N" // Header
	  << setw(18) << "| Density"
	  << setw(18) << "| Temp"
	  << setw(18) << "| Number"
	  << setw(18) << "| Mass"
	  << setw(18) << "| Volume"
	  << endl;
      out << "# " << setw(6) << 1;
      for (unsigned k=2; k<7; k++) out << "| " << setw(16) << k;
      out << endl;
      for (unsigned j=0; j<tphase.size(); j++) {
	out << setw(8) << j << setw(18) << tphase[j].first;
	for (unsigned k=0; k<Nphase; k++) 
	  out << setw(18) << tphase[j].second[k];
	out << endl;
      }
    }
    
  } else {
    unsigned num = tmfpst.size();
    unsigned nmb = derat.size();
    MPI_Send(&nmb, 1, MPI_UNSIGNED, 0, 38, MPI_COMM_WORLD);
    MPI_Send(&num, 1, MPI_UNSIGNED, 0, 39, MPI_COMM_WORLD);
    MPI_Send(&tsrat[0],  num, MPI_DOUBLE, 0, 40, MPI_COMM_WORLD);
    MPI_Send(&tdens[0],  num, MPI_DOUBLE, 0, 41, MPI_COMM_WORLD);
    MPI_Send(&tvolc[0],  num, MPI_DOUBLE, 0, 42, MPI_COMM_WORLD);
    MPI_Send(&ttemp[0],  num, MPI_DOUBLE, 0, 43, MPI_COMM_WORLD);
    MPI_Send(&tdelt[0],  num, MPI_DOUBLE, 0, 44, MPI_COMM_WORLD);
    MPI_Send(&tseln[0],  num, MPI_DOUBLE, 0, 45, MPI_COMM_WORLD);
    MPI_Send(&derat[0],  nmb, MPI_DOUBLE, 0, 46, MPI_COMM_WORLD);

    vector<double> tmp(num);

    for (unsigned k=0; k<num; k++) tmp[k] = tphase[k].first;
    MPI_Send(&tmp[0],  num, MPI_DOUBLE, 0, 47, MPI_COMM_WORLD);
    for (unsigned l=0; l<Nphase; l++) {
      for (unsigned k=0; k<num; k++) tmp[k] = tphase[k].second[l];
      MPI_Send(&tmp[0],  num, MPI_DOUBLE, 0, 48+l, MPI_COMM_WORLD);
    }

    for (unsigned k=0; k<num; k++) tmp[k] = tmfpst[k].first;
    MPI_Send(&tmp[0],  num, MPI_DOUBLE, 0, 48+Nphase, MPI_COMM_WORLD);
    for (unsigned l=0; l<Nmfp; l++) {
      for (unsigned k=0; k<num; k++) tmp[k] = tmfpst[k].second[l];
      MPI_Send(&tmp[0],  num, MPI_DOUBLE, 0, 49+Nphase+l, MPI_COMM_WORLD);
    }
  }
}

void Collide::EPSM(pHOT* tree, pCell* cell, int id)
{
  if (cell->bods.size()<2) return;

				// Compute mean and variance in each dimension
				// 
  vector<double> mvel(3, 0.0), disp(3, 0.0);
  double mass = 0.0;
  double Exes = 0.0;
  unsigned nbods = cell->bods.size();
  for (unsigned j=0; j<nbods; j++) {
    Particle* p = tree->Body(cell->bods[j]);
    for (unsigned k=0; k<3; k++) {
      mvel[k] += p->mass*p->vel[k];
      disp[k] += p->mass*p->vel[k]*p->vel[k];
    }
    mass += p->mass;
    if (use_exes>=0) {
      if (use_exes < static_cast<int>(p->dattrib.size())) {
	Exes += p->dattrib[use_exes];
	p->dattrib[use_exes] = 0;
      }
    }
  }

				// Can't do anything if the gas has no mass
  if (mass<=0.0) return;

  double Einternal = 0.0, Eratio;
  for (unsigned k=0; k<3; k++) {
    mvel[k] /= mass;
				// Disp is variance here
    disp[k] = (disp[k] - mvel[k]*mvel[k]*mass)/mass;

				// Crazy value?
    if (disp[k]<0.0) disp[k] = 0.0;

				// Total kinetic energy in COV frame
    Einternal += 0.5*mass*disp[k];
  }
				// Can't collide if with no internal energy
  if (Einternal<=0.0) return;

				// Correct 1d vel. disp. after cooling
				// 
  double Emin = 1.5*boltz*TFLOOR * mass/mp * 
    UserTreeDSMC::Munit/UserTreeDSMC::Eunit;

  if (Einternal - Emin > coolrate[id]+Exes)
    Eratio = (Einternal - coolrate[id]-Exes)/Einternal;
  else
    Eratio = min<double>(Emin, Einternal)/Einternal;
  
				// Compute the mean 1d vel.disp. from the
				// distribution
  double mdisp = 0.0;
  for (unsigned k=0; k<3; k++) {
    if (disp[k]>0.0) mdisp += disp[k];
  }
  mdisp = sqrt(Eratio*mdisp/3.0);

				// Sanity check
				// 
  if (mdisp<=0.0 || isnan(mdisp) || isinf(mdisp)) {
    cout << "Process " << myid  << " id " << id 
	 << ": crazy values, mdisp=" << mdisp << " Eratio=" << Eratio 
	 << " Eint=" << Einternal << " nbods=" << nbods << endl;
    return;
  }
				// Realize new velocities for all particles
				// 
  if (PULLIN) {
    double R=0.0, T=0.0;	// [Shuts up the compile-time warnings]
    const double sqrt3 = sqrt(3.0);

    if (nbods==2) {
      Particle* p1 = tree->Body(cell->bods[0]);
      Particle* p2 = tree->Body(cell->bods[1]);
      for (unsigned k=0; k<3; k++) {
	R = (*unit)();
	if ((*unit)()>0.5)
	  p1->vel[k] = mvel[k] + mdisp;
	else 
	  p1->vel[k] = mvel[k] - mdisp;
	p2->vel[k] = 2.0*mvel[k] - p1->vel[k];
      }

    } else if (nbods==3) {
      Particle* p1 = tree->Body(cell->bods[0]);
      Particle* p2 = tree->Body(cell->bods[1]);
      Particle* p3 = tree->Body(cell->bods[2]);
      double v2, v3;
      for (unsigned k=0; k<3; k++) {
	T = 2.0*M_PI*(*unit)();
	v2 = M_SQRT2*mdisp*cos(T);
	v3 = M_SQRT2*mdisp*sin(T);
	p1->vel[k] = mvel[k] - M_SQRT2*v2/sqrt3;
	p2->vel[k] = p1->vel[k] + (sqrt3*v2 - v3)/M_SQRT2;
	p3->vel[k] = p2->vel[k] + M_SQRT2*v3;
      }
    } else if (nbods==4) {
      Particle* p1 = tree->Body(cell->bods[0]);
      Particle* p2 = tree->Body(cell->bods[1]);
      Particle* p3 = tree->Body(cell->bods[2]);
      Particle* p4 = tree->Body(cell->bods[3]);
      double v2, v3, e2, e4, v4;
      for (unsigned k=0; k<3; k++) {
	R = (*unit)();
	e2 = mdisp*mdisp*(1.0 - R*R);
	T = 2.0*M_PI*(*unit)();
	v2 = sqrt(2.0*e2)*cos(T);
	v3 = sqrt(2.0*e2)*sin(T);
	p1->vel[k] = mvel[k] - sqrt3*v2/2.0;
	p2->vel[k] = p1->vel[k] + (2.0*v2 - M_SQRT2*v3)/sqrt3;
	e4 = mdisp*mdisp*R*R;
	if ((*unit)()>0.5) v4 =  sqrt(2.0*e4);
	else               v4 = -sqrt(2.0*e4);
	p3->vel[k] = p2->vel[k] + (sqrt3*v3 - v4)/M_SQRT2;
	p4->vel[k] = p3->vel[k] + M_SQRT2*v4;
      }

    } else {

      Particle *Pm1, *P00, *Pp1;
      vector<double> Tk, v(nbods), e(nbods);
      int kmax, dim, jj;
      bool Even = (nbods/2*2 == nbods);

      for (int k=0; k<3; k++) {
	if (Even) { 
				// Even
	  kmax = nbods;
	  dim = kmax/2-1;
	  Tk = vector<double>(dim);
	  for (int m=0; m<dim; m++) 
	    Tk[m] = pow((*unit)(), 1.0/(kmax/2 - m - 1.5));
	} else {			
				// Odd
	  kmax = nbods-1;
	  dim = kmax/2-1;
	  Tk = vector<double>(dim);
	  for (int m=0; m<dim; m++) 
	    Tk[m] = pow((*unit)(), 1.0/(kmax/2 - m - 1.0));
	}
      
	e[1] = mdisp*mdisp*(1.0 - Tk[0]);
	T = 2.0*M_PI*(*unit)();
	v[1] = sqrt(2.0*e[1])*cos(T);
	v[2] = sqrt(2.0*e[1])*sin(T);

	P00 = tree->Body(cell->bods[0]);
	Pp1 = tree->Body(cell->bods[1]);

	P00->vel[k] = mvel[k] - sqrt(nbods-1)*v[1]/sqrt(nbods);
	Pp1->vel[k] = P00->vel[k] + (sqrt(nbods)*v[1] - sqrt(nbods-2)*v[2])/sqrt(nbods-1);

	double prod = 1.0;
	for (int j=4; j<kmax-1; j+=2) {
	  jj = j-1;

	  Pm1 = tree->Body(cell->bods[jj-2]);
	  P00 = tree->Body(cell->bods[jj-1]);
	  Pp1 = tree->Body(cell->bods[jj  ]);

	  prod *= Tk[j/2-2];
	  e[jj] = mdisp*mdisp*(1.0 - Tk[j/2-1])*prod;
	  T = 2.0*M_PI*(*unit)();
	  v[jj]   = sqrt(2.0*e[jj])*cos(T);
	  v[jj+1] = sqrt(2.0*e[jj])*sin(T);
	  
	  P00->vel[k] = Pm1->vel[k] + 
	    (sqrt(3.0+nbods-j)*v[jj-1] - sqrt(1.0+nbods-j)*v[jj]  )/sqrt(2.0+nbods-j);
	  Pp1->vel[k] = P00->vel[k] +
	    (sqrt(2.0+nbods-j)*v[jj  ] - sqrt(    nbods-j)*v[jj+1])/sqrt(1.0+nbods-j);
	}

	prod *= Tk[kmax/2-2];
	e[kmax-1] = mdisp*mdisp*prod;

	if (Even) {
	  if ((*unit)()>0.5) v[nbods-1] =  sqrt(2.0*e[kmax-1]);
	  else               v[nbods-1] = -sqrt(2.0*e[kmax-1]);
	} else {
	  T = 2.0*M_PI*(*unit)();
	  v[nbods-2] = sqrt(2.0*e[kmax-1])*cos(T);
	  v[nbods-1] = sqrt(2.0*e[kmax-1])*sin(T);

	  Pm1 = tree->Body(cell->bods[nbods-4]);
	  P00 = tree->Body(cell->bods[nbods-3]);

	  P00->vel[k] = Pm1->vel[k] + (2.0*v[nbods-3] - M_SQRT2*v[nbods-2])/sqrt3;
	}

	Pm1 = tree->Body(cell->bods[nbods-3]);
	P00 = tree->Body(cell->bods[nbods-2]);
	Pp1 = tree->Body(cell->bods[nbods-1]);

	P00->vel[k] = Pm1->vel[k] + (sqrt3*v[nbods-2] - v[nbods-1])/M_SQRT2;
	Pp1->vel[k] = P00->vel[k] + M_SQRT2*v[nbods-1];
      }
    }

    // End Pullin algorithm

  } else {

				// Realize a distribution with internal
				// dispersion only
    vector<double> Tmvel(3, 0.0);
    vector<double> Tdisp(3, 0.0);
    for (unsigned j=0; j<nbods; j++) {
      Particle* p = tree->Body(cell->bods[j]);
      for (unsigned k=0; k<3; k++) {
	p->vel[k] = mdisp*(*norm)();
	Tmvel[k] += p->mass*p->vel[k];
	Tdisp[k] += p->mass*p->vel[k]*p->vel[k];
      }
    }
				// Compute mean and variance
				// 
    double Tmdisp = 0.0;
    for (unsigned k=0; k<3; k++) {
      Tmvel[k] /= mass;
      Tdisp[k] = (Tdisp[k] - Tmvel[k]*Tmvel[k]*mass)/mass;
      Tmdisp += Tdisp[k];
    }
    Tmdisp = sqrt(Tmdisp/3.0);

				// Sanity check
				// 
    if (Tmdisp<=0.0 || isnan(Tmdisp) || isinf(Tmdisp)) {
      cout << "Process " << myid  << " id " << id 
	   << ": crazy values, Tmdisp=" << Tmdisp << " mdisp=" << mdisp 
	   << " nbods=" << nbods << endl;
      return;
    }
				// Enforce energy and momentum conservation
				// 
    for (unsigned j=0; j<nbods; j++) {
      Particle* p = tree->Body(cell->bods[j]);
      for (unsigned k=0; k<3; k++)
	p->vel[k] = mvel[k] + (p->vel[k]-Tmvel[k])*mdisp/Tmdisp;
    }

  }
				// Record diagnostics
				// 
  lostSoFar_EPSM[id] += Einternal*(1.0 - Eratio);
  decelT[id] += Einternal*(1.0 - Eratio);
  epsm1T[id] += nbods;
  Nepsm1T[id]++;
}

void Collide::list_sizes()
{
  string sname = runtag + ".collide_storage";
  for (int n=0; n<numprocs; n++) {
    if (myid==n) {
      ofstream out(sname.c_str(), ios::app);
      if (out) {
	out << setw(18) << tnow
	    << setw(6)  << myid;
	list_sizes_proc(&out);
	out << endl;
	if (myid==numprocs-1) out << endl;
	out.close();
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
}


void Collide::list_sizes_proc(ostream* out)
{
  *out << setw(12) << numcnt.size()
       << setw(12) << colcnt.size()
       << setw(12) << tsrat.size()
       << setw(12) << derat.size()
       << setw(12) << tdens.size()
       << setw(12) << tvolc.size()
       << setw(12) << ttemp.size()
       << setw(12) << tdelt.size()
       << setw(12) << tseln.size()
       << setw(12) << tphase.size()
       << setw(12) << (tphaseT.size() ? tphaseT[0].size() : (size_t)0)
       << setw(12) << tmfpst.size()
       << setw(12) << (tmfpstT.size() ? tmfpstT[0].size() : (size_t)0)
       << setw(12) << (numcntT.size() ? numcntT[0].size() : (size_t)0)
       << setw(12) << (colcntT.size() ? colcntT[0].size() : (size_t)0)
       << setw(12) << error1T.size()
       << setw(12) << col1T.size()
       << setw(12) << epsm1T.size()
       << setw(12) << Nepsm1T.size()
       << setw(12) << KEtotT.size()
       << setw(12) << KElostT.size()
       << setw(12) << tmassT.size()
       << setw(12) << decelT.size()
       << setw(12) << (mfpratT.size() ? mfpratT[0].size() : (size_t)0)
       << setw(12) << (tsratT.size() ? tsratT[0].size() : (size_t)0)
       << setw(12) << (tdensT.size() ? tdensT[0].size() : (size_t)0)
       << setw(12) << (tvolcT.size() ? tvolcT[0].size() : (size_t)0)
       << setw(12) << (ttempT.size() ? ttempT[0].size() : (size_t)0)
       << setw(12) << (tselnT.size() ? tselnT[0].size() : (size_t)0)
       << setw(12) << (deratT.size() ? deratT[0].size() : (size_t)0)
       << setw(12) << (tdeltT.size() ? tdeltT[0].size() : (size_t)0)
       << setw(12) << (tdispT.size() ? tdispT[0].size() : (size_t)0)
       << setw(12) << tdiag.size()
       << setw(12) << tdiag1.size()
       << setw(12) << tdiag0.size()
       << setw(12) << tcool.size()
       << setw(12) << tcool1.size()
       << setw(12) << tcool0.size()
       << setw(12) << (cellist.size() ? cellist[0].size() : (size_t)0)
       << setw(12) << disptot.size()
       << setw(12) << lostSoFar_EPSM.size();
}
