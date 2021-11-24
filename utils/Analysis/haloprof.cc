/*****************************************************************************
 *  Description:
 *  -----------
 *
 *  Read in coefficients and compute gnuplot slices, and compute
 *  volume for rendering
 *
 *
 *  Call sequence:
 *  -------------
 *
 *  Parameters:
 *  ----------
 *
 *
 *  Returns:
 *  -------
 *
 *
 *  Notes:
 *  -----
 *
 *
 *  By:
 *  --
 *
 *  MDW 11/28/08
 *
 ***************************************************************************/

				// C++/STL headers
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>

using namespace std;

                                // System libs
#include <sys/time.h>
#include <sys/resource.h>

				// MDW classes
#include <numerical.H>
#include <ParticleReader.H>
#include <interp.H>
#include <massmodel.H>
#include <SphereSL.H>
#include <DataGrid.H>
#include <localmpi.H>
#include <foarray.H>
#include <cxxopts.H>
#include <global.H>
  
// Globals
//
std::string OUTFILE;
double RMIN, RMAX;
int OUTR, LMAX, NMAX, MMAX, L1, L2, N1, N2;
bool VOLUME, SURFACE, PROBE;

// Center offset
//
std::vector<double> c0 = {0.0, 0.0, 0.0};

class Histogram
{
public:

  std::vector<double> dataXY, dataXZ, dataYZ;
  double R, dR;
  int N;
  
  Histogram(int N, double R) : N(N), R(R)
  {
    N = std::max<int>(N, 2);
    dR = 2.0*R/(N-1);		// Want grid points to be on bin centers

    dataXY.resize(N*N);
    dataXZ.resize(N*N);
    dataYZ.resize(N*N);

    Reset();
  }

  void Reset() {
    std::fill(dataXY.begin(), dataXY.end(), 0.0);
    std::fill(dataXZ.begin(), dataXZ.end(), 0.0);
    std::fill(dataYZ.begin(), dataYZ.end(), 0.0);
  }

  void Syncr() { 
    if (myid==0) {
      MPI_Reduce(MPI_IN_PLACE, &dataXY[0], dataXY.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
      MPI_Reduce(MPI_IN_PLACE, &dataXZ[0], dataXZ.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
      MPI_Reduce(MPI_IN_PLACE, &dataYZ[0], dataYZ.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    }
    else {
      MPI_Reduce(&dataXY[0],         NULL, dataXY.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
      MPI_Reduce(&dataXZ[0],         NULL, dataXZ.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
      MPI_Reduce(&dataYZ[0],         NULL, dataYZ.size(), MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    }

  }

  void Add(double x, double y, double z, double m)
  {
    if (x < -R-0.5*dR or x >= R+0.5*dR or
	y < -R-0.5*dR or y >= R+0.5*dR or
	z < -R-0.5*dR or z >= R+0.5*dR) return;

    int indX = static_cast<int>(floor((x + R + 0.5*dR)/dR));
    int indY = static_cast<int>(floor((y + R + 0.5*dR)/dR));
    int indZ = static_cast<int>(floor((z + R + 0.5*dR)/dR));

    indX = std::max<int>(indX, 0);
    indY = std::max<int>(indY, 0);
    indZ = std::max<int>(indZ, 0);

    indX = std::min<int>(indX, N-1);
    indY = std::min<int>(indY, N-1);
    indZ = std::min<int>(indZ, N-1);

    dataXY[indX*N + indY] += m;
    dataXZ[indX*N + indZ] += m;
    dataYZ[indY*N + indZ] += m;
  }

};


void add_particles(PRptr reader, const std::string comp, vector<Particle>& p, Histogram& h)
{
  // Request particle type
  //
  reader->SelectType(comp);

  // Begin reading particles
  //
  auto part = reader->firstParticle();
    
  while (part) {

    // Copy the Particle
    //
    p.push_back(Particle(*part));
    
    // Add to histogram
    //
    if (part) h.Add(part->pos[0] - c0[0],
		    part->pos[1] - c0[1],
		    part->pos[2] - c0[2],
		    part->mass);

    // Iterate
    //
    part = reader->nextParticle();
  }

  // Synchronize histogram
  //
  h.Syncr();
}

enum class Slice {xy, xz, yz};

void write_output(SphereSL& ortho, int icnt, double time, Histogram& histo,
		  Slice slice=Slice::xy)
{
  unsigned ncnt = 0;

  // ==================================================
  // Setup for output files
  // ==================================================
  
  ostringstream sstr;
  sstr << "." << std::setw(4) << std::setfill('0') << std::right << icnt;

  const int nout1 = 10;
  const int nout2 = 13;
  string suffix[13] = {"p0", "p1", "p", "fr", "ft", "fp", "d0", "d1", "d", "dd",
		       "histoXY", "histoXZ", "histoYZ"};

  if (VOLUME) {
      
    // ==================================================
    // Write volume density
    // ==================================================
    
    double v;
    int valid = 1;
      
    double dR = 2.0*RMAX/(OUTR-1);
    double x, y, z, r, phi, costh;
    double p0, p1, d0, d1, pl, fr, ft, fp;
    
    std::vector<double> data(nout1*OUTR*OUTR*OUTR, 0.0);
    
    for (int k=0; k<OUTR; k++) {
      
      z = -RMAX + dR*k;
      
      for (int l=0; l<OUTR; l++) {
	
	y = -RMAX + dR*l;
	
	for (int j=0; j<OUTR; j++) {
	  
	  x = -RMAX + dR*j;
	  
	  r = sqrt(x*x + y*y + z*z) + 1.0e-18;
	  costh = z/r;
	  phi = atan2(y, x);
	  
	  ortho.all_eval(r, costh, phi, d0, d1, p0, p1, fr, ft, fp, L1, L2, N1, N2);
	  
	  data[((0*OUTR + k)*OUTR + l)*OUTR + j] = p0;
	  data[((1*OUTR + k)*OUTR + l)*OUTR + j] = p1;
	  data[((2*OUTR + k)*OUTR + l)*OUTR + j] = fr;
	  data[((3*OUTR + k)*OUTR + l)*OUTR + j] = ft;
	  data[((4*OUTR + k)*OUTR + l)*OUTR + j] = fp;
	  data[((5*OUTR + k)*OUTR + l)*OUTR + j] = d0;
	  data[((6*OUTR + k)*OUTR + l)*OUTR + j] = d1;
	  if (d0>0.0)
	    data[((7*OUTR + k)*OUTR + l)*OUTR + j] = d1/d0;
	  else
	    data[((7*OUTR + k)*OUTR + l)*OUTR + j] = 0.0;
	}
      }
    }
    
    
    if (myid==0)
      MPI_Reduce(MPI_IN_PLACE, &data[0], nout1*OUTR*OUTR*OUTR, MPI_DOUBLE, MPI_SUM, 
		 0, MPI_COMM_WORLD);
    
    MPI_Reduce(&data[0], NULL, nout1*OUTR*OUTR*OUTR, MPI_DOUBLE, MPI_SUM, 
	       0, MPI_COMM_WORLD);
    
    if (myid==0) {

      DataGrid vtk(OUTR, OUTR, OUTR, -RMAX, RMAX, -RMAX, RMAX, -RMAX, RMAX);

      std::vector<double> tmp(OUTR*OUTR*OUTR);

      for (int n=0; n<nout1; n++) {
	for (int k=0; k<OUTR; k++) {
	  for (int l=0; l<OUTR; l++) {
	    for (int j=0; j<OUTR; j++) {
	      tmp[(j*OUTR + l)*OUTR + k] = data[((n*OUTR + k)*OUTR + l)*OUTR + j];
	    }
	  }
	}
	vtk.Add(tmp, suffix[n]);
      }

      std::ostringstream sout;
      sout << outdir + "/" + OUTFILE + "_volume";
      vtk.Write(sout.str());
    }

  }
  
  if (SURFACE) {
    
    // ==================================================
    // Write surface profile
    //   --- in plane ---
    // ==================================================
    
    double v;
    float f;
    
    double dR = 2.0*RMAX/OUTR;
    double x, y, z=0.0, r, phi, costh;
    double p0, p1, d0, d1, fr, ft, fp;
    
    vector<double> data(nout1*OUTR*OUTR, 0.0);
    
    for (int l=0; l<OUTR; l++) {
      
      double y0 = -RMAX + dR*(0.5+l);
      
      for (int j=0; j<OUTR; j++) {
	
	if ((ncnt++)%numprocs == myid) {
	  
	  double x0 = -RMAX + dR*(0.5+j);
	  
	  switch (slice) {
	  case Slice::xy:
	    x = x0;
	    y = y0;
	    break;
	  case Slice::xz:
	    x = x0;
	    y = 0.0;
	    z = y0;
	    break;
	  case Slice::yz:
	    x = 0.0;
	    y = x0;
	    z = y0;
	    break;
	  }

	  r = sqrt(x*x + y*y + z*z) + 1.0e-18;
	  costh = z/r;
	  phi = atan2(y, x);

	  ortho.all_eval(r, costh, phi, d0, d1, p0, p1, fr, ft, fp, L1, L2, N1, N2);
	  
	  data[(0*OUTR+l)*OUTR+j] = p0;
	  data[(1*OUTR+l)*OUTR+j] = p1;
	  data[(2*OUTR+l)*OUTR+j] = p0 + p1;
	  data[(3*OUTR+l)*OUTR+j] = fr;
	  data[(4*OUTR+l)*OUTR+j] = ft;
	  data[(5*OUTR+l)*OUTR+j] = fp;
	  data[(6*OUTR+l)*OUTR+j] = d0;
	  data[(7*OUTR+l)*OUTR+j] = d1;
	  data[(8*OUTR+l)*OUTR+j] = d0 + d1;

	  if (d0>0.0)
	    data[(9*OUTR+l)*OUTR+j] = d1/d0;
	  else
	    data[(9*OUTR+l)*OUTR+j] = 0.0;
	}
      }
    }
    
    if (myid==0) 
      MPI_Reduce(MPI_IN_PLACE, &data[0], nout1*OUTR*OUTR,
		 MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    else
      MPI_Reduce(&data[0], NULL, nout1*OUTR*OUTR,
		 MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    
    
    if (myid==0) {
      
      std::vector<double> dataXY(OUTR*OUTR);

      DataGrid vtkXY(OUTR, OUTR, 1, -RMAX, RMAX, -RMAX, RMAX, 0, 0);

      for (int n=0; n<nout1; n++) {
	for (int j=0; j<OUTR; j++) {
	  for (int l=0; l<OUTR; l++) {
	    dataXY[j*OUTR + l] = data[(n*OUTR+j)*OUTR+l];
	  }
	}
	vtkXY.Add(dataXY, suffix[n]);
      }

      vtkXY.Add(histo.dataXY, suffix[10]);
      vtkXY.Add(histo.dataXZ, suffix[11]);
      vtkXY.Add(histo.dataYZ, suffix[12]);

      std::ostringstream sout;
      sout << outdir + "/" + runtag + "_" + OUTFILE + "_surface" + sstr.str();
      vtkXY.Write(sout.str());

    }
  }

  if (PROBE) {
    
    // ==================================================
    // Write line profile along three axes
    // ==================================================
    
    double v;
    float f;
    bool use_log;
    double dR;

    if (RMIN>0.0) {
      use_log = true;
      dR = (log(RMAX) - log(RMIN))/(OUTR-1);
    } else {
      use_log = false;
      dR = RMAX/(OUTR-1);
    }
      
    double r, phi, costh;
    double p0, p1, d0, d1, fr, ft, fp;
    int indx;
    
    vector<double> data(3*nout1*OUTR, 0.0);
    
    for (int l=0; l<OUTR; l++) {
      
      r = dR*l;
      if (use_log) r = RMIN*exp(r);
      
      if ((ncnt++)%numprocs == myid) {
	  
	indx = 3*nout1*l;

	costh = 0.0;
	phi   = 0.0;
	ortho.all_eval(r, costh, phi, d0, d1, p0, p1, fr, ft, fp);
	  
	data[indx + 0] = p0;
	data[indx + 1] = p1;
	data[indx + 2] = p0 + p1;
	data[indx + 3] = fr;
	data[indx + 4] = ft;
	data[indx + 5] = fp;
	data[indx + 6] = d0;
	data[indx + 7] = d1;
	data[indx + 8] = d0 + d1;
	if (d0>0.0)
	  data[indx + 9] = d1/d0;
	else
	  data[indx + 9] = 0.0;

	costh = 0.0;
	phi   = 0.5*M_PI;
	ortho.all_eval(r, costh, phi, d0, d1, p0, p1, fr, ft, fp);
	  
	indx += nout1;
	data[indx + 0] = p0;
	data[indx + 1] = p1;
	data[indx + 2] = p0 + p1;
	data[indx + 3] = fr;
	data[indx + 4] = ft;
	data[indx + 5] = fp;
	data[indx + 6] = d0;
	data[indx + 7] = d1;
	data[indx + 8] = d0 + d1;
	if (d0>0.0)
	  data[indx + 9] = d1/d0;
	else
	  data[indx + 9] = 0.0;

	costh = 1.0;
	phi   = 0.0;
	ortho.all_eval(r, costh, phi, d0, d1, p0, p1, fr, ft, fp);
	  
	indx += nout1;
	data[indx + 0] = p0;
	data[indx + 1] = p1;
	data[indx + 2] = p0 + p1;
	data[indx + 3] = fr;
	data[indx + 4] = ft;
	data[indx + 5] = fp;
	data[indx + 6] = d0;
	data[indx + 7] = d1;
	if (d0>0.0)
	  data[indx + 8] = d1/d0;
	else
	  data[indx + 8] = 0.0;

      }
    }
    
    if (myid==0)
      MPI_Reduce(MPI_IN_PLACE, &data[0], 3*nout1*OUTR, MPI_DOUBLE, MPI_SUM, 
	       0, MPI_COMM_WORLD);
    else
      MPI_Reduce(&data[0], NULL, 3*nout1*OUTR, MPI_DOUBLE, MPI_SUM, 
	       0, MPI_COMM_WORLD);
    
    if (myid==0) {
      
      vector<string> names(nout1);
      for (int i=0; i<nout1; i++) {
	names[i] = outdir + "/" + OUTFILE + "." + suffix[i] + ".cut" + sstr.str();
      }

      foarray out(names, true);

      for (int l=0; l<OUTR; l++) {
	
	r = dR*l;
	if (use_log) r = RMIN*exp(r);
      
	indx = 3*nout1*l;
	
	for (int n=0; n<nout1; n++)
	  out[n] << setw(18) << time << setw(18) << r
		 << setw(18) << data[indx + 0*nout1 + n]
		 << setw(18) << data[indx + 1*nout1 + n]
		 << setw(18) << data[indx + 2*nout1 + n]
		 << endl;
      }
      
      for (int n=0; n<nout1; n++) out[n] << endl;
    }
  }
}


int
main(int argc, char **argv)
{
  
#ifdef DEBUG
  sleep(20);
#endif  
  
  double snr, rscale, Hexp;
  int NICE, LMAX, NMAX, NPART;
  int beg, end, stride;
  std::string MODFILE, INDEX, dir("."), cname, coefs, fileType, filePrefix;


  // ==================================================
  // Parse command line or input parameter file
  // ==================================================
  
  std::ostringstream sout;
  sout << std::string(60, '-') << std::endl
       << "Compute halo potential, force and density profiles from " << std::endl
       << "phase-space output files" << std::endl
       << std::string(60, '-') << std::endl;
  
  cxxopts::Options options(argv[0], sout.str());
  
  options.add_options()
    ("h,help", "Print this help message")
    ("v,verbose", "Verbose and diagnostic output for covariance computation")
    ("CONLY", "make coefficient file only")
    ("xy", "print x-y slice for surface fields (default)")
    ("xz", "print x-z slice for surface fields")
    ("yz", "print y-z slice for surface fields")
    ("F,filetype", "input file type",
     cxxopts::value<std::string>(fileType)->default_value("PSPout"))
    ("P,prefix", "prefix for phase-space files",
     cxxopts::value<std::string>(filePrefix)->default_value("OUT"))
    ("NICE", "system priority",
     cxxopts::value<int>(NICE)->default_value("0"))
    ("RMIN", "minimum radius for output",
     cxxopts::value<double>(RMIN)->default_value("0.0"))
    ("RMAX", "maximum radius for output",
     cxxopts::value<double>(RMAX)->default_value("0.1"))
    ("RSCALE", "coordinate mapping scale factor",
     cxxopts::value<double>(rscale)->default_value("0.067"))
    ("LMAX", "Maximum harmonic order for spherical expansion",
     cxxopts::value<int>(LMAX)->default_value("4"))
    ("NMAX", "Maximum radial order for spherical expansion",
     cxxopts::value<int>(NMAX)->default_value("12"))
    ("MMAX", "Maximum harmonic order",
     cxxopts::value<int>(MMAX)->default_value("4"))
    ("L1", "minimum l harmonic",
     cxxopts::value<int>(L1)->default_value("0"))
    ("L2", "maximum l harmonic",
     cxxopts::value<int>(L2)->default_value("1000"))
    ("N1", "minimum radial order",
     cxxopts::value<int>(N1)->default_value("0"))
    ("N2", "maximum radial order",
     cxxopts::value<int>(N2)->default_value("1000"))
    ("NPART", "Jackknife partition number for testing (0 means off, use standard eval)",
     cxxopts::value<int>(NPART)->default_value("0"))
    ("Hexp", "default Hall smoothing exponent",
     cxxopts::value<double>(Hexp)->default_value("1.0"))
    ("OUTR", "Number of radial points for output",
     cxxopts::value<int>(OUTR)->default_value("40"))
    ("PROBE", "Make traces along axes",
     cxxopts::value<bool>(PROBE)->default_value("false"))
    ("SURFACE", "Make equitorial and vertical slices",
     cxxopts::value<bool>(SURFACE)->default_value("true"))
    ("VOLUME", "Make volume grid",
     cxxopts::value<bool>(VOLUME)->default_value("false"))
    ("OUTFILE", "Filename prefix",
     cxxopts::value<string>(OUTFILE)->default_value("haloprof"))
    ("r,runtag", "Runtag name for phase-space files",
     cxxopts::value<string>(runtag)->default_value("run1"))
    ("outdir", "Output directory path",
     cxxopts::value<string>(outdir)->default_value("."))
    ("MODFILE", "Halo model file",
     cxxopts::value<string>(MODFILE)->default_value("SLGridSph.model"))
    ("beg", "initial PSP index",
     cxxopts::value<int>(beg)->default_value("0"))
    ("end", "final PSP index",
     cxxopts::value<int>(end)->default_value("99999"))
    ("stride", "PSP index stride",
     cxxopts::value<int>(stride)->default_value("1"))
    ("compname", "train on Component (default=stars)",
     cxxopts::value<std::string>(cname)->default_value("dark"))
    ("d,dir", "directory for phase-space files",
     cxxopts::value<std::string>(dir))
    ("c,coefs", "file of computed coefficients or to be computed (with CONLY)",
     cxxopts::value<std::string>(coefs))
    ("S,snr", "if not negative: do a SNR cut on the PCA basis",
     cxxopts::value<double>(snr)->default_value("-1.0"))
    ("C,center", "Accumulation center",
     cxxopts::value<std::vector<double> >(c0))
    ("diff", "render the difference between the trimmed and untrimmed basis")
    ;
  
  
  // ==================================================
  // MPI preliminaries
  // ==================================================

  local_init_mpi(argc, argv);
  
  cxxopts::ParseResult vm;

  try {
    vm = options.parse(argc, argv);
  } catch (cxxopts::OptionException& e) {
    if (myid==0) std::cout << "Option error: " << e.what() << std::endl;
    MPI_Finalize();
    exit(-1);
  }

  // ==================================================
  // Print help message and exit
  // ==================================================

  if (vm.count("help")) {
    std::cout << std::endl << options.help() << std::endl;
    return 0;
  }

  bool rendering = true;
  if (vm.count("coefs") and vm.count("CONLY")) {
    rendering = false;
  }

  bool SPL = false;
  if (vm.count("SPL")) SPL = true;
  if (vm.count("OUT")) SPL = false;

  bool Hall = false;
  if (vm.count("Hall")) Hall = true;

  bool verbose = false;
  if (vm.count("verbose")) verbose = true;

  Slice slice = Slice::xy;
  if (vm.count("xy")) slice = Slice::xy;
  if (vm.count("xz")) slice = Slice::xz;
  if (vm.count("yz")) slice = Slice::yz;

  if (vm.count("center")) {
    if (c0.size() != 3) {
      if (myid==0) std::cout << "Center vector needs three components"
			     << std::endl;
      MPI_Finalize();
      exit(-1);
    }

    if (myid==0) {
      std::cout << "Using center: ";
      for (auto v : c0) std::cout << " " << v << " ";
      std::cout << std::endl;
    }
  }

  // ==================================================
  // Nice process
  // ==================================================

  if (NICE>0)
    setpriority(PRIO_PROCESS, 0, NICE);

  // ==================================================
  // Make SL expansion
  // ==================================================

  auto halo = std::make_shared<SphericalModelTable>(MODFILE);

  SphereSL::mpi  = true;
  SphereSL::NUMR = 4000;
  SphereSL::HEXP = Hexp;

  SphereSL ortho(halo, LMAX, NMAX, 1, rscale, true, NPART);
  
  std::string file;

  std::ofstream outcoef;	// Coefficient file

  if (vm.count("coefs")) {
    std::string coeffile = outdir + "/" + coefs + ".coefs";
				// Set exceptions to be thrown on failure
    outcoef.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
      outcoef.open(coeffile, std::ofstream::out | std::ofstream::app);
    } catch (std::system_error& e) {
      std::cerr << e.code().message() << std::endl;
    }
  }

  for (int indx=beg; indx<=end; indx+=stride) {

    // ==================================================
    // Phase-space input stream
    // ==================================================

    int iok = 1;

    auto file1 = ParticleReader::fileNameCreator
      (fileType, indx, myid, dir, runtag, filePrefix);

    if (verbose and myid==0) {
      std::cout << "Will try to open <" << file1 << ">" << std::endl;
    }
    {
      std::ifstream in(file1);
      if (!in) {
	cerr << "Error opening <" << file1 << ">" << endl;
	iok = 0;
      }
    }
    
    MPI_Bcast(&iok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (iok==0) break;

    // ==================================================
    // Open phase-space file
    // ==================================================
    //
    PRptr reader;

    try {
      reader = ParticleReader::createReader(fileType, file1, myid, verbose);
    }
    catch (std::runtime_error &error) {
      std::cerr << error.what() << std::endl;
      MPI_Finalize();
      exit(-1);
    }

    double tnow = reader->CurrentTime();
    if (myid==0) cout << "Beginning partition [time=" << tnow
		      << ", index=" << indx << "] . . . "  << flush;
    
    Histogram histo(OUTR, RMAX);
    std::vector<Particle> particles;

    add_particles(reader, cname, particles, histo);
    if (myid==0) cout << "done" << endl;

    //------------------------------------------------------------ 

    if (myid==0) cout << "Accumulating particle positions . . . " << flush;

    ortho.reset_coefs();
    for (auto &i : particles) {
      ortho.accumulate(i.pos[0], i.pos[1], i.pos[2], i.mass);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (myid==0) cout << "done" << endl;
  
    //------------------------------------------------------------ 

    if (myid==0) cout << "Making coefficients . . . " << flush;
    ortho.make_coefs();
    MPI_Barrier(MPI_COMM_WORLD);
    if (myid==0) cout << "done" << endl;

    //------------------------------------------------------------ 
    //
    // Coefficient trimming
    //
    if (snr>=0.0) {

      if (myid==0) {
	std::cout << "Computing SNR=" << snr;
	if (Hall) std::cout << " using Hall smoothing, " << flush;
	else      std::cout << " using truncation, " << flush;
      }
    
      ortho.make_covar(verbose);

      // Get the snr trimmed coefficients
      //
      Eigen::MatrixXd origc = ortho.retrieve_coefs();
      Eigen::MatrixXd coefs = ortho.get_trimmed(snr, ortho.getMass(), Hall);

      std::cout << "power in trim=" << ortho.get_power(snr, ortho.getMass())
		<< " . . . ";

      if (vm.count("diff")) coefs = coefs - origc;
      ortho.install_coefs(coefs);
    }

    //------------------------------------------------------------ 

    double time = 0.0;
    if (myid==0) {
      time = reader->CurrentTime();
      if (outcoef.good()) {
	cout << "Writing coefficients . . . " << flush;
	try {
	  ortho.dump_coefs(time, outcoef);
	} catch (std::system_error& e) {
	  std::cerr << e.code().message() << std::endl;
	}
	cout << "done" << endl;
      }
    }
    if (rendering) {
      if (myid==0) cout << "Writing output . . . " << flush;
      write_output(ortho, indx, time, histo, slice);
      if (myid==0) cout << "done" << endl;
    }
    MPI_Barrier(MPI_COMM_WORLD);

    //------------------------------------------------------------ 

  } // Dump loop

  MPI_Finalize();

  return 0;
}

