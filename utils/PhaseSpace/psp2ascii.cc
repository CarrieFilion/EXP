/*
  Separate a psp structure to ascii components

  MDWeinberg 06/10/02, 11/24/19
*/

using namespace std;

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <random>
#include <list>

#include <StringTok.H>
#include <header.H>
#include <PSP.H>
#include <cxxopts.H>

int
main(int argc, char **argv)
{
  char *prog = argv[0];
  double time=1e20;
  bool verbose = false;
  bool input   = false;
  std::string cname("comp"), new_dir("./"), filename;

  // Parse command line
  //
  cxxopts::Options options("psp2ascii", "Convert PSP output to ascii for analysis");
  
  options.add_options()
    ("help,h", "this help message")
    ("verbose,v", "verbose output")
    ("input,a", "use input format")
    ("time,t", "desired input time slice",
     cxxopts::value<double>(time)->default_value("1.0e+20"))
    ("outname,o", "prefix name for each component (default: comp)",
     cxxopts::value<std::string>(cname)->default_value("comp"))
    ("dirname,d", "replacement SPL file directory",
     cxxopts::value<std::string>(new_dir)->default_value("./"))
    ("filename,f", "input PSP file",
     cxxopts::value<std::string>(filename))
    ;

  auto vm = options.parse(argc, argv);

  // Print help message and exit
  //
  if (vm.count("help")) {
    if (myid==0) {
      std::cout << options.help() << std::endl;
    }
    return 1;
  }

  if (vm.count("verbose")) {
    verbose = true;
  }

  if (vm.count("input")) {
    input = true;
  }


  if (vm.count("filename")) {

    std::ifstream in(filename);
    if (!in) {
      cerr << "Error opening file <" << filename << "> for input\n";
      exit(-1);
    }
    
    if (verbose) cerr << "Using filename: " << filename << endl;

  } else {
    std::cout << options.help() << std::endl;
    exit(-1);
  }
				// Parse the PSP file
				// ------------------
  PSPptr psp;
  if (filename.find("SPL") != std::string::npos)
    psp = std::make_shared<PSPspl>(filename, new_dir);
  else
    psp = std::make_shared<PSPout>(filename);

				// Now write a summary
				// -------------------
  if (verbose) {

    psp->PrintSummary(cerr);
    
    cerr << "\nPSP file <" << filename << "> has time <" 
	 << psp->CurrentTime() << ">\n";
  }

				// Dump ascii for each component
				// -----------------------------
  PSPstanza *stanza;
  SParticle* part;

  for (stanza=psp->GetStanza(); stanza!=0; stanza=psp->NextStanza()) {
    
				// Open an output file for this stanza
				// -----------------------------------
    ostringstream oname;
    oname << cname << "." << stanza->name << '\0';
    ofstream out(oname.str().c_str());
    out.setf(ios::scientific);
    out.precision(10);

    if (!out) {
      cerr << "Couldn't open output name <" << oname.str() << ">\n";
      exit(-1);
    }
				// Print the header
				// ----------------
    out << setw(15) << stanza->comp.nbod 
	<< setw(10) << stanza->comp.niatr 
	<< setw(10) << stanza->comp.ndatr 
	<< endl;

    for (part=psp->GetParticle(); part!=0; part=psp->NextParticle()) {

      if (not input and stanza->index_size)
	out << std::setw(18) << part->indx();
      out << std::setw(18) << part->mass();
      for (int i=0; i<3; i++) out << std::setw(18) << part->pos(i);
      for (int i=0; i<3; i++) out << std::setw(18) << part->vel(i);
      if (not input) out << std::setw(18) << part->phi();
      for (int i=0; i<part->niatr(); i++) out << std::setw(12) << part->iatr(i);
      for (int i=0; i<part->ndatr(); i++) out << std::setw(18) << part->datr(i);

      out << std::endl;		// End the record
    }
    
  }
  
  return 0;
}
  
