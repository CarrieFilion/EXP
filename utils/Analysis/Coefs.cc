#include "Coefs.H"
#include <iostream>
#include <cstring>
#include <cmath>

bool Coefs::read(std::istream& in, bool verbose)
{
  // Save initial stream position
  //
  auto curpos = in.tellg();

  // Attempt to read coefficient magic number
  //
  const unsigned int cmagic = 0xc0a57a3;
  unsigned int tmagic;

  in.read(reinterpret_cast<char*>(&tmagic), sizeof(unsigned int));

  if (tmagic == cmagic) {

    // YAML size
    //
    unsigned ssize;
    in.read(reinterpret_cast<char*>(&ssize), sizeof(unsigned int));

    // Make and read char buffer
    //
    auto buf = boost::make_unique<char[]>(ssize+1);
    in.read(buf.get(), ssize);
    buf[ssize] = 0;		// Null terminate

    YAML::Node node = YAML::Load(buf.get());
      
    // Get parameters
    //
    time = node["time"].as<double>();
    nmax = node["nmax"].as<int>();
    mmax = node["mmax"].as<int>();

    if (verbose)
      std::cerr << "New header: T=" << time << " nmax=" << nmax
		<< " mmax=" << mmax << std::endl;
  } else {

    // Rewind file
    //
    in.clear();
    in.seekg(curpos);

    CylCoefHeader header;
    in.read((char *)&header, sizeof(CylCoefHeader));

    time = header.time;
    nmax = header.nmax;
    mmax = header.mmax;

    if (verbose)
      std::cerr << "Old header: T=" << time << " nmax=" << nmax
		<< " mmax=" << mmax << std::endl;
  }

  if (not in) return false;

  cos_c.resize(mmax+1);
  sin_c.resize(mmax+1);
  
  for (int mm=0; mm<=mmax; mm++) {

    cos_c[mm].resize(nmax);
    in.read((char *)&cos_c[mm][0], sizeof(double)*nmax);

    if (mm) {
      sin_c[mm].resize(nmax);
      in.read((char *)&sin_c[mm][0], sizeof(double)*nmax);
    }
  }

  if (verbose) {
    if (in)
      std::cerr << "Coefficients successfully read at T=" << time << std::endl;
    else
      std::cerr << "Coefficient read FAILED at T=" << time << std::endl;
  }

  return true;
}

bool SphCoefs::read(std::istream& in, bool exp_type)
{
  // Save initial stream position
  //
  auto curpos = in.tellg();

  // Coefficient magic number
  //
  const unsigned int cmagic = 0xc0a57a2;

  // Try to read magic #
  //
  unsigned int tmagic;
  in.read(reinterpret_cast<char *>(&tmagic), sizeof(unsigned int));

  // Found new-style coefficient file
  //
  if (cmagic == tmagic) {

    // Read YAML string size
    //
    unsigned int hsize;
    in.read(reinterpret_cast<char *>(&hsize), sizeof(unsigned int));
    
    // Create buffer
    //
    auto buf = boost::make_unique<char[]>(hsize+1);

    // Read YAML string
    //
    in.read(buf.get(), hsize);
    buf[hsize] = 0;		// Null terminate

    YAML::Node node = YAML::Load(buf.get());
      
    // Get parameters
    //
    header.Lmax  = node["lmax"  ].as<int>();
    header.nmax  = node["nmax"  ].as<int>();
    header.tnow  = node["time"  ].as<double>();
    header.scale = node["scale" ].as<double>();

    std::fill(header.id, header.id+64, 0);
    std::string ID = node["id"].as<std::string>();
    strncpy(header.id, ID.c_str(), std::min<int>(64, ID.size()));

  } else {

    // Rewind file
    //
    in.clear();
    in.seekg(curpos);

    in.read((char *)&header, sizeof(SphCoefHeader));
  }

  if (not in) return false;

  coefs.resize((header.Lmax+1)*(header.Lmax+1));
  for (auto & v : coefs) v.resize(header.nmax);

  for (int ir=0; ir<header.nmax; ir++) {
    for (int l=0; l<(header.Lmax+1)*(header.Lmax+1); l++)
      in.read((char *)&coefs[l][ir], sizeof(double));
  }

  if (exp_type) {
    int k = 0;
    for (int l=0; l<=header.Lmax; l++) {
      for (int m=0; m<=l; m++) {
	double fac = sqrt( (0.5*l+0.25)/M_PI * 
			   exp(lgamma(1.0+l-m) - lgamma(1.0+l+m)) );

	if (m != 0) fac *= M_SQRT2;

	// Cosine terms
	for (int ir=0; ir<header.nmax; ir++) coefs[k][ir] *= fac;
	k++;

	// Sine terms
	if (m != 0) {
	  for (int ir=0; ir<header.nmax; ir++) coefs[k][ir] *= fac;
	  k++;
	}
      }
    }
  }

  return true;
}