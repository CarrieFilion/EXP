
#include "SphericalCoefs.H"

bool SphericalCoefs::Coefs::read(std::istream& in)
{
  SphCoefHeader header;
  in.read((char *)&header, sizeof(SphCoefHeader));
  if (not in) return false;
  
  time = header.tnow;
  nmax = header.nmax;
  lmax = header.Lmax;
  
  for (int ll=0; ll<=lmax; ll++) {
    for (int mm=0; mm<=ll; mm++) {
      LMkey key(ll, mm);
      
      cos_c[key].resize(nmax);
      in.read((char *)&cos_c[key][0], sizeof(double)*nmax);
      if (not in) return false;
    
      if (mm) {
	sin_c[key].resize(nmax);
	in.read((char *)&sin_c[key][0], sizeof(double)*nmax);
	if (not in) return false;
      }
    }
  }
  
  return true;
}


SphericalCoefs::SphericalCoefs(const std::string& file, unsigned stride)
{
  std::ifstream in(file);

  unsigned counter = 0;

  while (in.good()) {
    CoefPtr c = std::make_shared<Coefs>();
    if (not c->read(in)) break;
    if (counter++ % stride == 0) data[c->time] = c;
  }

  lmax   = data.begin()->second->lmax;
  nmax   = data.begin()->second->nmax;
  ntimes = data.size();

  for (auto v : data) {
    double Time = v.second->time;
    times.push_back(Time);
    coefs[Time].resize((lmax+1)*(lmax+1));

    int cnt = 0;
    for (int l=0; l<=lmax; l++) {
      for (int m=0; m<=l; m++) {
	LMkey lmk  = {l, m};
	coefs[Time][cnt++] = data[Time]->cos_c[lmk];
	if (m) coefs[Time][cnt++] = data[Time]->sin_c[lmk];
      }
    }
  }
}

SphericalCoefs::D2vector SphericalCoefs::interpolate(const double time)
{
  if (time < times.front() or time > times.back()) {
    std::cerr << "Time=" << time << " is offgrid [" << times.front()
	      << ", " << times.back() << "]" << std::endl;
  }

  auto it = std::lower_bound(times.begin(), times.end(), time);
  auto lo = it, hi = it;

  if (hi == times.end()) {
    hi = times.end() - 1;
    lo = hi - 1;
  } else hi++;

  double A = (*hi - time)/(*hi - *lo);
  double B = (time - *lo)/(*hi - *lo);

  int iA = std::distance(times.begin(), lo);
  int iB = std::distance(times.begin(), hi);

  D2vector & cA = coefs[times[iA]];
  D2vector & cB = coefs[times[iB]];

  D2vector ret((lmax+1)*(lmax+1));

  for (int i=0; i<(lmax+1)*(lmax+1); i++) {
    ret[i].resize(nmax);
    for (int n=0; n<nmax; n++) ret[i][n] = A*cA[i][n] + B*cB[i][n];
  }

  return ret;
}
