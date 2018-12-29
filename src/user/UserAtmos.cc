#include <sys/timeb.h>
#include <math.h>
#include <sstream>

#include "expand.h"

#include <UserAtmos.H>


UserAtmos::UserAtmos(const YAML::Node& conf) : ExternalForce(conf)
{

  id = "SphericalHalo";		// Halo model file

				// Components of the acceleration
  g.push_back(-1.0);
  g.push_back( 0.0);
  g.push_back( 0.0);

  compname = "";		// Default component

  initialize();

				// Look for the fiducial component for
				// centering
  c0 = 0;
  for (auto c : comp->components) {
    if ( !compname.compare(c->name) ) {
      c0 = c;
      break;
    }
  }
  
  if (!c0) {
    cerr << "Process " << myid << ": can't find desired component <"
	 << compname << ">" << endl;
    MPI_Abort(MPI_COMM_WORLD, 35);
  }


  userinfo();
}

UserAtmos::~UserAtmos()
{
}

void UserAtmos::userinfo()
{
  if (myid) return;		// Return if node master node

  print_divider();

  cout << "** User routine UNIFORM GRAVITATIONAL FIELD initialized" ;
  cout << ", applied to component <" << compname << ">";
  cout << endl;

  cout << "Acceleration constants (gx , gy , gz) = (" 
       << g[0] << " , " 
       << g[1] << " , " 
       << g[2] << " ) " << endl; 

  print_divider();
}

void UserAtmos::initialize()
{
  try {
    if (conf["gx"])             g[0]               = conf["gx"].as<double>();
    if (conf["gy"])             g[1]               = conf["gy"].as<double>();
    if (conf["gz"])             g[2]               = conf["gz"].as<double>();
    if (conf["compname"])       compname           = conf["compname"].as<string>();
  }
  catch (YAML::Exception & error) {
    if (myid==0) std::cout << "Error parsing parameters in UserAtmos: "
			   << error.what() << std::endl;
    MPI_Finalize();
    exit(-1);
  }
}


void UserAtmos::determine_acceleration_and_potential(void)
{
  exp_thread_fork(false);
}


void * UserAtmos::determine_acceleration_and_potential_thread(void * arg) 
{
  unsigned nbodies = cC->Number();
  int id = *((int*)arg);
  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;
  double pot, pos[3];
  
  PartMapItr it = cC->Particles().begin();

  for (int q=0   ; q<nbeg; q++) it++;
  for (int q=nbeg; q<nend; q++) {
    unsigned long i = (it++)->first;
				// If we are multistepping, compute accel 
				// only at or below this level

    if (multistep && (cC->Part(i)->level < mlevel)) continue;


    // Compute potential (up to a some constant)
    pot = 0.0;
    for (int k=0; k<3; k++) {
      pos[k] = cC->Pos(i, k);	// Inertial by default
      if (c0) pos[k] -= c0->center[k];
      pot += -g[k]*pos[k];
    }

    // Add external accerlation
    for (int k=0; k<3; k++) cC->AddAcc(i, k, g[k]);

    // Add external potential
    cC->AddPotExt(i, pot);

  }

  return (NULL);
}


extern "C" {
  ExternalForce *makerAtmos(const YAML::Node& conf)
  {
    return new UserAtmos(conf);
  }
}

class proxyatmos { 
public:
  proxyatmos()
  {
    factory["useratmos"] = makerAtmos;
  }
};

proxyatmos p;
