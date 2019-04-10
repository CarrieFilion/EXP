#include <mpi.h>
#include <string>

#include <coef.H>
#include <ComponentContainer.H>
#include <ExternalForce.H>
#include <ExternalCollection.H>
#include <OutputContainer.H>
#include <BarrierWrapper.H>
#include <chkTimer.H>

				// Numerical parameters

unsigned nbodmax = 1000000;	// Maximum number of bodies; this is not
				// an intrinsic limitation just a sanity
				// value

int nsteps = 500;		// Number of steps to execute
int nscale = 20;		// Number of steps between rescaling
int nthrds = 2;			// Number of POSIX threads (minimum: 1)
int ngpus  = 0;	                // Number of GPUs per node (0 means use all available)
int nbalance = 0;		// Steps between load balancing
int nreport = 0;		// Steps between particle reporting
double dbthresh = 0.05;		// Load balancing threshold (5% by default)
double dtime = 0.1;		// Default time step size
unsigned nbits = 32;		// Number of bits per dimension
unsigned pkbits = 6;		// Number of bits for parition
unsigned PFbufsz = 40000;	// ParticleFerry buffer size in particles


bool restart = false;		// Restart from a checkpoint
bool use_cwd = false;		// Use Node 0's current working directory on all nodes
int NICE = 0;			// Niceness level (default: 0)
int VERBOSE = 1;		// Chattiness for standard output
bool initializing = false;	// Used by force methods to do "private things"
				// before the first step (e.g. run through
				// coefficient evaluations even when
				// self_consistent=false)
double runtime = 72.0;		// Total alloted runtime

				// Files
string homedir = "./";
string infile = "restart.in";
string parmfile = "PARAM";
string ratefile = "processor.rates";
string outdir = "./";
string runtag = "newrun";
string ldlibdir = ".";

double tnow;			// Per step variables
int this_step;
int psdump = -1;
				// Global center of mass
double mtot;
double *gcom = new double [3];
double *gcov = new double [3];
bool global_cov = false;
bool eqmotion = true;
unsigned char stop_signal = 0;
unsigned char dump_signal = 0;
unsigned char quit_signal = 0;
				// Multistep variables
unsigned multistep = 0;
int centerlevl = -1;
bool DTold = false;
double dynfracS = 1.00;
double dynfracD = 1000.0;
double dynfracV = 0.01;
double dynfracA = 0.03;
double dynfracP = 0.05;
int Mstep = 0;
int mstep = 0;
vector<int> mfirst, mintvl, stepL, stepN;
vector< vector<bool> > mactive;
vector< vector<int> > dstepL, dstepN;


				// Multithreading data structures for
				// incr_position and incr_velocity
/*struct thrd_pass_posvel 
{
  double dt;
  int mlevel;
  int id;
};*/

vector<thrd_pass_posvel> posvel_data;
vector<pthread_t> posvel_thrd;

				// MPI variables
int is_init=1;
int numprocs, slaves, myid, proc_namelen;
char processor_name[MPI_MAX_PROCESSOR_NAME];

MPI_Comm MPI_COMM_SLAVE;

				// List of host names and ranks
std::map<std::string, std::vector<int> > nameMap;

				// List of sibling ranks
std::vector<int> siblingList;


char threading_on = 0;
pthread_mutex_t mem_lock;

CoefHeader coefheader;
CoefHeader2 coefheader2;

ComponentContainer *comp = 0;
ExternalCollection *external = 0;
OutputContainer    *output = 0;
YAML::Node          parse;

map<string, maker_t *, less<string> > factory;
map<string, maker_t *, less<string> >::iterator fitr;

string lastPS;
CheckpointTimer chktimer;
string restart_cmd;

MPI_Datatype MPI_EXP_KEYTYPE;

BarrierWrapper *barrier = 0;
bool barrier_check = false;
bool barrier_debug = false;
bool barrier_extra = false;
bool barrier_label = true;
bool barrier_light = true;
bool barrier_quiet = true;

bool cuda_prof     = false;
bool debug_wait    = false;
bool main_wait     = false;
bool mpi_wait      = false;
bool fpe_trap      = false;
bool fpe_trace     = false;
bool fpe_wait      = false;

bool ignore_info   = false;

int  rlimit_val    = 0;
int  cuStreams     = 3;
bool use_cuda      = true;
