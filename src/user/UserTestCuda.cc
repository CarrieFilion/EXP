#include <expand.h>
#include <global.H>
#include <localmpi.h>
#include <ExternalCollection.H>

#include "UserTestCuda.H"

int UserTestCuda::total = 0;

UserTestCuda::UserTestCuda(const YAML::Node& conf) : ExternalForce(conf)
{
  id = "TestCuda";
  total++;
  instance = total;
  maxcall = -1;
  callno  = 0;

  initialize();

  if (myid==0) 
    std::cout << "** Just made a UserTestCuda Instance=" << instance
	      <<  std::endl;
}

UserTestCuda::~UserTestCuda()
{
  total--;
}

void UserTestCuda::initialize()
{
  try {
    if (conf["maxcall"])        maxcall            = conf["maxcall"].as<int>();
  }
  catch (YAML::Exception & error) {
    if (myid==0) std::cout << "Error parsing parameters in UserTestCuda: "
			   << error.what()         << std::endl
			   << std::string(60, '-') << std::endl
			   << "Config node"        << std::endl
			   << std::string(60, '-') << std::endl
			   << conf                 << std::endl
			   << std::string(60, '-') << std::endl;

    MPI_Finalize();
    exit(-1);
  }
}

void * UserTestCuda::determine_acceleration_and_potential_thread(void * arg) 
{
  int id = *((int*)arg);

  // Only chat on the master step
  //
  if (multistep and mstep) return(NULL); 

  // Should we say something now?
  //
  if (maxcall>0 and callno++ < maxcall) {
#if HAVE_LIBCUDA == 1
    cuda_user_test(myid, id, tnow, instance);
#else
    std::cout << "Process " << myid 
	      << ", id=" << id << ": Time=" << tnow 
	      << ", Instance=" << instance << std::endl;
#endif
  }

  return (NULL);
}


extern "C" {
  ExternalForce *makerTestCuda(const YAML::Node& conf)
  {
    return new UserTestCuda(conf);
  }
}

class proxytestcuda { 
public:
  proxytestcuda()
  {
    factory["usertestcuda"] = makerTestCuda;
  }
};

proxytestcuda p;
