/*
  Separate a psp structure and make a 1-d histogram

  MDWeinberg 11/24/19
*/

using namespace std;

#include <cstdlib>

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <random>
#include <string>
#include <list>
#include <map>

#include <cxxopts.H>		// Option parsing
#include <libvars.H>		// EXP library globals
#include <header.H>		// PSP headers
#include <PSP.H>		// PSP class

int
main(int ac, char **av)
{
  char *prog = av[0];
  bool verbose = false;
  bool areal   = false;
  bool vnorm   = false;
  bool snorm   = false;
  bool use_sph = false;
  bool use_cyl = false;
  bool use_ctr = false;
  bool use_cov = false;
  std::string cname, new_dir;
  int axis, numb, comp;
  double pmin, pmax;

  // Parse command line
  //
  cxxopts::Options options(prog, "Separate a psp structure and make a 1-d histogram\n");

  options.add_options()
    ("h,help", "produce help message")
    ("r,radial", "use spherical radius")
    ("R,cylindrical", "cylindrical radius")
    ("A,areal", "areal average")
    ("D,vnorm", "compute density for radial bins")
    ("S,snorm", "compute surface density for cylindrical bins")
    ("v,verbose", "verbose output")
    ("C,com", "compute position center from most bound particles")
    ("V,cov", "compute velocity center from most bound particles")
    ("p,pmin", "minimum position along axis",
     cxxopts::value<double>(pmin)->default_value("-100.0"))
    ("P,pmax", "maximum position along axis",
     cxxopts::value<double>(pmax)->default_value("100.0"))
    ("b,bins", "number of bins",
     cxxopts::value<int>(numb)->default_value("40"))
    ("i,comp", "index for extended value",
     cxxopts::value<int>(comp)->default_value("9"))
    ("c,name", "component name",
     cxxopts::value<std::string>(cname)->default_value("comp"))
    ("a,axis", "histogram along desired axis: x=1, y=2, z=3",
     cxxopts::value<int>(axis)->default_value("3"))
    ("f,files", "input files",
     cxxopts::value< std::vector<std::string> >())
    ("d,dir", "rewrite directory location for SPL files",
     cxxopts::value<std::string>(new_dir)->default_value("./"))
    ;

  cxxopts::ParseResult vm;

  try {
    vm = options.parse(ac, av);
  } catch (cxxopts::OptionException& e) {
    std::cout << "Option error: " << e.what() << std::endl;
    exit(-1);
  }

  if (vm.count("help")) {
    std::cout << options.help() << std::endl;
    std::cout << "Example: " << std::endl;
    std::cout << "\t" << av[0]
	      << " --output=out.bod" << std::endl;
    return 1;
  }

  if (vm.count("radial")) {
    use_sph = true;
    use_cyl = false;
    snorm   = false;
    if (vm.count("vnorm")) vnorm = true;
  }

  if (vm.count("cylindrical")) {
    use_sph = false;
    use_cyl = true;
    if (vm.count("snorm")) snorm = true;
    vnorm   = false;
  }

  if (vm.count("areal")) {
    areal = true;
  }

  if (vm.count("verbose")) {
    verbose = true;
  }

  if (vm.count("com")) {
    use_ctr = true;
  }

  if (vm.count("cov")) {
    use_cov = true;
  }
				// Axis sanity check 
				// ------------------
  if (axis<1) axis = 1;
  if (axis>3) axis = 3;

  std::vector<std::string> files = vm["files"].as< std::vector<std::string> >();

  bool first = true;
  
  for (auto file : files ) {

    if (verbose) cerr << "Using filename: " << file << endl;

				// Parse the PSP file
				// ------------------
    PSPptr psp;
    if (file.find("SPL") != std::string::npos)
      psp = std::make_shared<PSPspl>(file, new_dir);
    else
      psp = std::make_shared<PSPout>(file);


				// Now write a summary
				// -------------------
    if (verbose) {
      
      psp->PrintSummary(cerr);
    
      std::cerr << "\nPSP file <" << file << "> has time <" 
	   << psp->CurrentTime() << ">\n";
    }

				// Dump ascii for each component
				// -----------------------------
  
    double rtmp, mass, dp=(pmax - pmin)/numb;
    vector<double> pos(3), vel(3);
    int itmp, icnt, iv;

				// Make the array
				// --------------

    vector<float> value(numb, 0), bmass(numb, 0);

    PSPstanza *stanza;
    SParticle* part;

    for (stanza=psp->GetStanza(); stanza!=0; stanza=psp->NextStanza()) {
    
      if (stanza->name != cname) continue;

      std::vector<double> com = {0, 0, 0}, cov = {0, 0, 0};
      if (use_ctr) {
	std::map<double, std::array<double, 7>> posn;
	for (part=psp->GetParticle(); part!=0; part=psp->NextParticle()) {
	  posn[part->phi()] = {part->pos(0), part->pos(1), part->pos(2),
			       part->vel(0), part->vel(1), part->vel(2),
			       part->mass()};
	}
	double total = 0.0;
	auto p = posn.begin();
	for (int i=0; i<floor(posn.size()*0.05); i++, p++) {
	  for (int k=0; k<3; k++) {
	    com[k] += p->second[6] * p->second[k];
	    if (use_cov) cov[k] += p->second[6] * p->second[3+k];
	  }
	  total += p->second[6];
	}
	if (total>0.0) {
	  for (int k=0; k<3; k++) {
	    com[k] /= total;
	    cov[k] /= total;
	  }
	}
	std::cout << "COM is: "	<< com[0] << ", " << com[1] << ", "<< com[2] << std::endl;
	std::cout << "COV is: "	<< cov[0] << ", " << cov[1] << ", "<< cov[2] << std::endl;
      }
      
      for (part=psp->GetParticle(); part!=0; part=psp->NextParticle()) {

	double val = 0.0;
	if (use_sph) {
	  for (int k=0; k<3; k++) {
	    val += (part->pos(k) - com[k]) * (part->pos(k) - com[k]);
	  }
	  val = sqrt(val);
	  iv = static_cast<int>( floor( (val - pmin)/dp ) );
	}
	else if (use_cyl) {
	  for (int k=0; k<2; k++) {
	    val += (part->pos(k) - com[k]) * (part->pos(k) - com[k]);
	  }
	  val = sqrt(val);
	  iv = static_cast<int>( floor( (val - pmin)/dp ) );
	}
	else {
	  val = part->pos(axis-1) - com[axis-1];
	  iv = static_cast<int>( floor( (part->pos(axis-1) - com[axis-1] - pmin)/dp ) );
	}

	if (iv < 0 || iv >= numb) continue;

	bmass[iv] += 1.0;

	if (comp == 0)
	  value[iv] += part->mass();
	else if (comp <= 3)
	  value[iv] += part->pos(comp-1) - com[comp-1];
	else if (comp <= 6)
	  value[iv] += part->vel(comp-4) - cov[comp-4];
	else if (comp == 7)
	  value[iv] += part->phi();
	else if (part->niatr() && comp <= 7 + part->niatr())
	  value[iv] += part->iatr(comp-8);
	else if (part->ndatr())
	  value[iv] += part->datr(comp-8-part->niatr());
      }
    
    }
    
    //
    // Output
    //
    const size_t fw = 12;
    const size_t sw =  9;
    float p, f, m=0.0;

    if (first) {
      std::cout << setw(fw) << "Position"
		<< setw(fw) << "Value"
		<< setw(fw) << "Mass"
		<< std::endl;

      std::cout << setw(fw) << std::string(sw, '-')
		<< setw(fw) << std::string(sw, '-')
		<< setw(fw) << std::string(sw, '-')
		<< std::endl;

      first = false;
    }

    for (int i=0; i<numb; i++) {
      p  = pmin + dp*(0.5+i);
      f  = 0.0;
      m += bmass[i];
      if (vnorm) {
	double rmax3 = pow(pmin+dp*(1.0+i), 3.0);
	double rmin3 = pow(pmin+dp*(0.0+i), 3.0);
	f = value[i]/(4.0*M_PI/3.0*(rmax3 - rmin3));
      } else if (snorm) {
	double rmax2 = pow(pmin+dp*(1.0+i), 2.0);
	double rmin2 = pow(pmin+dp*(0.0+i), 2.0);
	f = value[i]/(M_PI*(rmax2 - rmin2));
      } else if (areal) {
	f = value[i]/dp;
      } else {
	if (bmass[i] > 0.0) f = value[i]/bmass[i];
      }
      cout << setw(fw) << p
	   << setw(fw) << f
	   << setw(fw) << m
	   << endl;
    }
    cout << endl;
  }

  return 0;
}
