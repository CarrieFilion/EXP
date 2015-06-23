/*
  Test radiative cross section 
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <tuple>

#include <boost/program_options.hpp>

#include "atomic_constants.H"
#include "Ion.H"

namespace po = boost::program_options;

#include <mpi.h>

int numprocs, myid;

int main (int ac, char **av)
{
  //===================
  // MPI preliminaries 
  //===================

  MPI_Init(&ac, &av);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  if (myid) MPI_Finalize();


  std::string oname;

  std::string cmd_line;
  for (int i=0; i<ac; i++) {
    cmd_line += av[i];
    cmd_line += " ";
  }

  unsigned short Z, C;
  double emin, emax;
  bool eVout = false;
  int num;

  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h",		"produce help message")
    ("eV",		"print results in eV")
    ("Z,Z",             po::value<unsigned short>(&Z)->default_value(2),
     "atomic number")
    ("C,C",             po::value<unsigned short>(&C)->default_value(3),
     "ionic charge")
    ("Emin,e",          po::value<double>(&emin)->default_value(0.001),
     "minimum energy (Rydbergs)")
    ("Emax,E",          po::value<double>(&emax)->default_value(100.0),
     "maximum energy (Rydbergs)")
    ("Num,N",          po::value<int>(&num)->default_value(200),
     "number of evaluations")
    ("output,o",	po::value<std::string>(&oname)->default_value("out.bods"),
     "body output file")
    ;


  po::variables_map vm;

  try {
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);    
  } catch (po::error& e) {
    std::cout << "Option error: " << e.what() << std::endl;
    MPI_Finalize();
    return -1;
  }

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    std::cout << "Example: Helium II recombination" << std::endl;
    std::cout << "\t" << av[0]
	      << " -Z 2 -C 2" << std::endl;
    MPI_Finalize();
    return 1;
  }

  if (vm.count("eV")) {
    eVout = true;
  }

  std::string prefix("crossSection");
  std::string cmdFile = prefix + ".cmd_line";
  std::ofstream out(cmdFile.c_str());
  if (!out) {
    std::cerr << "testCrossSection: error opening <" << cmdFile
	      << "> for writing" << std::endl;
  } else {
    out << cmd_line << std::endl;
  }

  // Initialize CHIANTI
  //

  std::set<unsigned short> ZList = {1, 2};

  chdata ch;

  ch.createIonList(ZList);

  Ion::setRRtype("Verner");

  // Print cross section for requested ion
  //

				// Convert from Rydberg to eV
  const double ryd      = 27.2113845/2.0;

  emin = log(emin);
  emax = log(emax);

  double dE = (emax - emin)/(num - 1);

  lQ Q(Z, C);

  for (int i=0; i<num; i++) {
    double E   = exp(emin + dE*i);
    double EeV = E * ryd;

    std::vector<double> RE1 = ch.IonList[Q]->radRecombCross(EeV, 0);
    std::vector<double> PI1 = ch.IonList[Q]->photoIonizationCross(EeV, 0);

    std::cout << std::setw(16) << (eVout ? EeV : E)
	      << std::setw(16) << 0.0001239841842144513*1.0e8/EeV
	      << std::setw(16) << RE1.back() * 1.0e+04 // Mb
	      << std::setw(16) << PI1.back() * 1.0e+04 // Mb
	      << std::endl;
  }

  MPI_Finalize();

  return 0;
}