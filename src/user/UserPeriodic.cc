#include <sys/timeb.h>
#include <math.h>
#include <sstream>

#include "expand.h"

#include <UserPeriodic.H>

//atomic mass unit in grams
const double amu = 1.660539e-24;
const double k_B = 1.3806488e-16;

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

UserPeriodic::UserPeriodic(string &line) : ExternalForce(line)
{
  (*barrier)("Periodic: BEGIN construction", __FILE__, __LINE__);

  id = "PeriodicBC";		// Periodic boundary condition ID

				// Sizes in each dimension
  L = vector<double>(3, 2.0);	
				// Center offset in each dimension
  offset = vector<double>(3, 1.0);

  bc = "ppp";			// Periodic BC in all dimensions

  comp_name = "";		// Default component (must be specified)

  nbin = 0;			// Number of bins in trace (0 means no trace)
  dT = 1.0;			// Interval for trace
  tcol = -1;			// Column for temperture info (ignored if <0)
  trace = false;		// Tracing off until signalled

  initialize();

				// Look for the fiducial component
  bool found = false;
  for (auto c : comp->components) {
    if ( !comp_name.compare(c->name) ) {
      c0 = c;
      found = true;
      break;
    }
  }

  if (!found) {
    cerr << "UserPeriodic: process " << myid 
	 << " can't find fiducial component <" << comp_name << ">" << endl;
    MPI_Abort(MPI_COMM_WORLD, 35);
  }
  
  // 
  // Initialize structures for trace data
  //
  if (nbin) {
    cbinT = vector< vector<unsigned> >(nthrds);
    mbinT = vector< vector<double>   >(nthrds);
    tbinT = vector< vector<double>   >(nthrds);
    for (int n=0; n<nthrds; n++) {
      cbinT[n] = vector<unsigned>(nbin, 0);
      mbinT[n] = vector<double>  (nbin, 0);
      tbinT[n] = vector<double>  (nbin, 0);
    }
    cbin = vector<unsigned>(nbin);
    mbin = vector<double>  (nbin);
    tbin = vector<double>  (nbin);
    Tnext = tnow;
    dX = L[0]/nbin;
  }

  userinfo();

  gen = new ACG(11+myid);
  unit = new Uniform(0.0, 1.0, gen);
  norm = new Normal(0.0, 1.0, gen);

  atomic_weights[1]  = 1.0079;
  atomic_weights[2]  = 4.0026;
  atomic_weights[3]  = 6.941;
  atomic_weights[4]  = 9.0122;
  atomic_weights[5]  = 10.811;
  atomic_weights[6]  = 12.011;
  atomic_weights[7]  = 14.007;
  atomic_weights[8]  = 15.999;
  atomic_weights[9]  = 18.998;
  atomic_weights[10] = 20.180;
  atomic_weights[11] = 22.990;
  atomic_weights[12] = 24.305;

  (*barrier)("Periodic: END construction", __FILE__, __LINE__);
}

UserPeriodic::~UserPeriodic()
{
}

void UserPeriodic::userinfo()
{
  if (myid) return;		// Return if node master node

  print_divider();

  cout << "** User routine PERIODIC BOUNDARY CONDITION initialized"
       << " using component <" << comp_name << ">";
  if (nbin) cout << " with gas trace, dT=" << dT 
		 << ", nbin=" << nbin << ", tcol=" << tcol;
  cout << endl;

  cout << "   Cube sides (x , y , z) = (" 
       << L[0] << " , " 
       << L[1] << " , " 
       << L[2] << " ) " << endl; 

  cout << "Center offset (x , y , z) = (" 
       << offset[0] << " , " 
       << offset[1] << " , " 
       << offset[2] << " ) " << endl; 

  cout << "Boundary type (x , y , z) = (" 
       << bc[0] << " , " 
       << bc[1] << " , " 
       << bc[2] << " ) " << endl;

  print_divider();
}

void UserPeriodic::initialize()
{
  string val;

  if (get_value("compname", val))	comp_name = val;

  if (get_value("sx", val))	        L[0] = atof(val.c_str());
  if (get_value("sy", val))	        L[1] = atof(val.c_str());
  if (get_value("sz", val))	        L[2] = atof(val.c_str());

  if (get_value("cx", val))	        offset[0] = atof(val.c_str());
  if (get_value("cy", val))	        offset[1] = atof(val.c_str());
  if (get_value("cz", val))	        offset[2] = atof(val.c_str());

  if (get_value("dT", val))	        dT = atof(val.c_str());
  if (get_value("nbin", val))	        nbin = atoi(val.c_str());
  if (get_value("tcol", val))	        tcol = atoi(val.c_str());

  if (get_value("vunit", val))		vunit = atof(val.c_str());
  if (get_value("temp", val))		temp = atof(val.c_str());

  thermal = false;

  if (get_value("btype", val)) {
    if (strlen(val.c_str()) >= 3) {
      for (int k=0; k<3; k++) {
	switch (val.c_str()[k]) {
	case 'p':
	  bc[k] = 'p';		// Periodic
	  break;
	case 'r':
	  bc[k] = 'r';		// Reflection
	  break;
	case 't':		// Thermal
	  bc[k] = 't';
	  thermal = true;
	  break;
	default:
	  bc[k] = 'v';		// Vacuum
	  break;
	}
      }
    }
  }
  
  // Check for thermal type
  if (thermal && c0->keyPos<0) {
    if (myid==0) {
      std::cerr << "UserPeriodic:: thermal wall specified but no gas species "
		<< "attribute specified.  Aborting . . ." << std::endl;
    }
    MPI_Abort(MPI_COMM_WORLD, 36);
  }

}


void UserPeriodic::determine_acceleration_and_potential(void)
{
  if (cC != c0) return;
  if (nbin && tnow>=Tnext) trace = true;
  exp_thread_fork(false);
  if (trace) write_trace();
  print_timings("UserPeriodic: thread timings");
}

void UserPeriodic::write_trace()
{
  for (int n=1; n<nthrds; n++) {
    for (int k=0; k<nbin; k++) {
      cbinT[0][k] += cbinT[n][k];
      mbinT[0][k] += mbinT[n][k];
      tbinT[0][k] += tbinT[n][k];
    }
  }

  MPI_Reduce(&cbinT[0][0], &cbin[0], nbin, MPI_UNSIGNED, MPI_SUM, 0, 
	     MPI_COMM_WORLD);

  MPI_Reduce(&mbinT[0][0], &mbin[0], nbin, MPI_DOUBLE,   MPI_SUM, 0, 
	     MPI_COMM_WORLD);

  MPI_Reduce(&tbinT[0][0], &tbin[0], nbin, MPI_DOUBLE,   MPI_SUM, 0, 
	     MPI_COMM_WORLD);

  if (myid==0) {
    ostringstream fout;
    fout << outdir << runtag << ".shocktube_trace";
    ofstream out(fout.str().c_str(), ios::app);
    for (int k=0; k<nbin; k++)
      out << setw(18) << tnow
	  << setw(18) << dX*(0.5+k) - offset[0]
	  << setw(18) << mbin[k]/(dX*L[1]*L[2])
	  << setw(18) << tbin[k]/(mbin[k]+1.0e-10)
	  << setw(10) << cbin[k]
	  << endl;
    out << endl;
  }

  //
  // Clean data structures for next call
  //
  for (int n=0; n<nthrds; n++) {
    for (int k=0; k<nbin; k++) {
      cbinT[n][k] = 0;
      mbinT[n][k] = tbinT[n][k] = 0.0;
    }
  }

  trace = false;		// Tracing off until
  Tnext += dT;			// tnow>=Tnext
}


void * UserPeriodic::determine_acceleration_and_potential_thread(void * arg) 
{
  unsigned nbodies = cC->Number();
  int id = *((int*)arg);
  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;

  
  thread_timing_beg(id);

  double pos, delta;
  PartMapItr it = cC->Particles().begin();
  
  std::advance(it, nbeg);

  for (int q=nbeg; q<nend; q++) {
    
				// Index for the current particle
    unsigned long i = (it++)->first;
    
    Particle *p = cC->Part(i);
    double   mi = 0.0;

    if (thermal and c0->keyPos>=0) {
      if (p->skey == Particle::defaultKey)
	p->skey = KeyConvert(p->iattrib[cC->keyPos]).getKey();
      mi = (atomic_weights[p->skey.first])*amu;
    }

    for (int k=0; k<3; k++) {

      // Ignore vacuum boundary dimensions
      //
      if (bc[k] == 'v') continue;

      // Increment so that the positions range
      // between 0 and L[k]
      //
      pos = p->pos[k] + offset[k];

      //
      // Reflection BC
      //
      if (bc[k] == 'r') {
	if (pos < 0.0) {
	  delta = -pos - L[k]*floor(-pos/L[k]);
	  p->pos[k] = delta - offset[k];
	  p->vel[k] *= -1.0;
	} 
	if (pos >= L[k]) {
	  delta = pos - L[k]*floor(pos/L[k]);
	  p->pos[k] =  L[k] - delta - offset[k];
	  p->vel[k] *= -1.0;
	}
      }

      //
      // Periodic BC
      //
      if (bc[k] == 'p') {
	if (pos < 0.0) {
	  p->pos[k] += L[k]*floor(1.0+fabs(pos/L[k]));
	  
	}
	if (pos >= L[k]) {
	  p->pos[k] += - L[k]*floor(fabs(pos/L[k]));
	}
      }

      //
      // Thermal BC (same as reflection with new thermal velocity)
      //
      if (bc[k] == 't') {
	if (pos < 0.0) {
	  delta = -pos - L[k]*floor(-pos/L[k]);
	  p->pos[k] = delta - offset[k];
	  for (int j = 0; j < 3; j++) {
	    if (j == k) {
	      p->vel[j] = -sgn(p->vel[j])*fabs(sqrt(k_B*temp/mi)*(*norm)()/vunit);
	    }
	    else  {
	      p->vel[j] = sqrt(k_B*temp/mi)*(*norm)()/vunit;
	    }
	  }
	} 
	if (pos >= L[k]) {
	  delta = pos - L[k]*floor(pos/L[k]);
	  p->pos[k] =  L[k] - delta - offset[k];
	  for (int j = 0; j < 3; j++) {
	    if (j == k) {
	      p->vel[j] = -sgn(p->vel[j])*fabs(sqrt(k_B*temp/mi)*(*norm)()/vunit);
	    }
	    else {
	      p->vel[j] = sqrt(k_B*temp/mi)*(*norm)()/vunit;
	    }
	  }
	}
      }
      
      //
      // Sanity check
      //
      if (p->pos[k] < -offset[k] || p->pos[k] >= L[k]-offset[k]) {
	cout << "Process " << myid << " id=" << id 
	     << ": Error in pos[" << k << "]=" << p->pos[k] << endl;
      }
    }

    //
    // Acccumlate data for shocktube trace
    //
    if (trace) {
      int indx = static_cast<int>(floor((p->pos[0]+offset[0])/dX));
      if (indx>=0 && indx<nbin) {
	cbinT[id][indx]++;
	mbinT[id][indx] += p->mass;
	if (tcol>=0 && tcol<static_cast<int>(p->dattrib.size()))
	  tbinT[id][indx] += p->mass*p->dattrib[tcol];
      }
    }

  }
  
  thread_timing_end(id);

  return (NULL);
}


extern "C" {
  ExternalForce *makerPeriodic(string& line)
  {
    return new UserPeriodic(line);
  }
}

class proxypbc { 
public:
  proxypbc()
  {
    factory["userperiodic"] = makerPeriodic;
  }
};

proxypbc p;
