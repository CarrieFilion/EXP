#include <iostream>
#include <fstream>
#include <iomanip>

#include <expand.h>

#include <OutCoef.H>

OutCoef::OutCoef(const YAML::Node& conf) : Output(conf)
{
  nint = 10;
  nintsub = std::numeric_limits<int>::max();
  filename = outdir + "outcoef." + runtag;
  tcomp = NULL;

  initialize();

  if (!tcomp) {
    if (myid==0) {
      std::cerr << "OutCoef: no component to trace\n";
    }
    MPI_Finalize();
    exit(112);
  }

  if (!(tcomp->force->HaveCoefDump())) {
    if (myid==0) {
      cerr << "OutCoef: no coefficients for this force\n";
    }
  }

}

void OutCoef::initialize()
{
  try {
    if (conf["filename"])     filename = conf["filename"].as<std::string>();
    if (conf["nint"])         nint     = conf["nint"].as<int>();
    if (conf["nintsub"])    nintsub  = conf["nintsub"].as<int>();
    if (conf["name"])
      {				// Search for desired component
	std::string tmp = conf["name"].as<std::string>();
	for (auto c : comp->components) {
	  if (!(c->name.compare(tmp))) tcomp  = c;
	}
      }
  }
  catch (YAML::Exception & error) {
    if (myid==0) std::cout << "Error parsing parameters in OutCoef: "
			   << error.what() << std::endl
			   << std::string(60, '-') << std::endl
			   << "Config node"        << std::endl
			   << std::string(60, '-') << std::endl
			   << conf                 << std::endl
			   << std::string(60, '-') << std::endl;
    MPI_Finalize();
    exit(-1);
  }
}

void OutCoef::Run(int n, int mstep, bool last)
{
  if (tcomp->force->PlayBack()) return;
  if (!(tcomp->force->HaveCoefDump())) return;
  if (n % nint != 0 && !last) return;
  if (mstep % nintsub !=0) return;

  MPI_Status status;

				// Open output file
  ofstream out;
  if (myid==0) {
    out.open(filename.c_str(), ios::out | ios::app);
    if (!out) {
      cout << "OutCoef: can't open file <" << filename << ">\n";
    }
  }
  
  tcomp->force->dump_coefs(out);

  out.close();
}
