#include <limits>
#include "expand.h"
#include <AxisymmetricBasis.H>
#include <VtkPCA.H>

AxisymmetricBasis:: AxisymmetricBasis(const YAML::Node& conf) : Basis(conf) 
{
  Lmax      = 4;
  nmax      = 10;
  dof       = 3;
  npca      = 500;
  pca       = false;
  pcadiag   = false;
  pcavtk    = false;
  pcajknf   = true;
  vtkfreq   = 1;
  tksmooth  = 3.0;
  tkcum     = 0.95;
  tk_type   = Null;
  sampT     = 0;

  string val;

  try {
    if (conf["Lmax"])     Lmax       = conf["Lmax"].as<int>();
    if (conf["nmax"])     nmax       = conf["nmax"].as<int>();
    if (conf["dof"])      dof        = conf["dof"].as<int>();
    if (conf["npca"])     npca       = conf["npca"].as<int>();
    if (conf["selector"]) pca        = conf["selector"].as<bool>();
    if (conf["pca"])      pca        = conf["pca"].as<bool>();
    if (conf["pcadiag"])  pcadiag    = conf["pcadiag"].as<bool>();
    if (conf["pcavtk"])   pcavtk     = conf["pcavtk"].as<bool>();
    if (conf["vtkfreq"])  vtkfreq    = conf["vtkfreq"].as<int>();
    if (conf["pcajknf"])  pcajknf    = conf["pcajknf"].as<bool>();
    if (conf["tksmooth"]) tksmooth   = conf["tksmooth"].as<double>();
    if (conf["tkcum"])    tkcum      = conf["tkcum"].as<double>();
    if (conf["tk_type"])  tk_type    = setTK(conf["tk_type"].as<std::string>());
  }
  catch (YAML::Exception & error) {
    if (myid==0) std::cout << "Error parsing parameters in AxisymmetricBasis: "
			   << error.what() << std::endl
			   << std::string(60, '-') << std::endl
			   << "Config node"        << std::endl
			   << std::string(60, '-') << std::endl
			   << conf                 << std::endl
			   << std::string(60, '-') << std::endl;

    MPI_Finalize();
    exit(-1);
  }


  sqnorm.setsize(0, Lmax, 1, nmax);
  for (int l=0; l<=Lmax; l++)
    for (int n=1; n<=nmax; n++) sqnorm[l][n] = 1.0;

  if (pca) {

    if (dof==3)
      Ldim = Lmax*(Lmax + 2) + 1;
    else
      Ldim = 2*Lmax + 1;
    
    weight = new Vector [Ldim];
    b_Hall = new Vector [Ldim];
    evec   = new Matrix [Ldim];
    
    for (int l=0; l<Ldim; l++) {
      weight[l].setsize(1, nmax);
      b_Hall[l].setsize(1, nmax);
      evec[l].setsize(1, nmax, 1, nmax);
    }

    smth.setsize(1, nmax);
    inv.setsize(1, nmax);
    eval.setsize(1, nmax);
    cuml.setsize(1, nmax);
    Tevec.setsize(1, nmax, 1, nmax);
    covar.setsize(1, nmax, 1, nmax);
    sqnorm.setsize(0, Lmax, 1, nmax);
      
    for (int l=0; l<=Lmax; l++)
      for (int n=1; n<=nmax; n++) sqnorm[l][n] = 1.0;

    if (myid==0) {

      const string types[] = {
	"Hall", 
	"VarianceCut", 
	"CumulativeCut",
	"VarianceWeighted", 
	"Null"};

      const string desc[] = {
	"Tapered signal-to-noise power defined by Hall",
	"Cut all coefficients below some S/N level",
	"Cut coefficients below some cumulative fraction",
	"Weight coefficients be S/N for S/N<1",
	"Compute the S/N but do not modify coefficients"};

      cout << "AxisymmetricBasis: using Hall type: " << types[tk_type] 
	   << " >>> " << desc[tk_type] << endl;
    }
  }
}

AxisymmetricBasis::~AxisymmetricBasis()
{
  vector<Matrix *>::iterator it;
  for (auto it : expcoefN) delete it;
  for (auto it : expcoefL) delete it;

  if (pca) {
    delete [] weight;
    delete [] b_Hall;
    delete [] evec;
  }
}


void AxisymmetricBasis::pca_hall(int compute)
{
  if (muse <= 0.0) return;

  std::ofstream out;		// PCA diag output
  std::ofstream cof;		// PCA diag output

  if (pcadiag and myid==0 and compute) {

    // Open the diag file
    ostringstream sout1, sout2;
    sout1 << runtag << ".pcadiag." << cC->id << "." << cC->name << ".pcalog";
    sout2 << runtag << ".pcadiag." << cC->id << "." << cC->name << ".pcamat";

    out.open(sout1.str().c_str(), ios::out | ios::app);
    cof.open(sout2.str().c_str(), ios::out | ios::app);

    if (out) {
      out << "#" << endl;
      out << "# Time=" << tnow << endl;
      out << "#" << endl;
      if (dof==3) out << right << "# " << setw(3) << "l";
      out << setw(5)  << "m" << setw(5) << "C/S" << setw(5) << "n";
      if (pcajknf)
	out << setw(18) << "jknf var"
	    << setw(18) << "cum"
	    << setw(18) << "jknf coef"
	    << setw(18) << "S/N"
	    << setw(18) << "B_Hall";
      else
	out << setw(18) << "var"
	    << setw(18) << "orig coef"
	    << setw(18) << "S/N"
	    << setw(18) << "proj var"
	    << setw(18) << "proj coef"
	    << setw(18) << "S/N";
      out << endl;
    } else {
      cout << "AxisymmetricBasis::pca_hall: could not open output file <"
	   << sout1.str() << ">" << endl
	   << "AxisymmetricBasis::pca_hall: continuing" << endl;
    }

    if (cof.good()) {
      cof << "#" << endl << std::right
	  << "# Time = " << tnow << endl
	  << "#" << endl << setprecision(4);
    } else {
      cout << "AxisymmetricBasis::pca_hall: could not open output file <"
	   << sout2.str() << ">" << endl
	   << "AxisymmetricBasis::pca_hall: continuing" << endl;
    }

  } // END: pcadiag file initialization

  VtkPCAptr vtkpca;

  static unsigned ocount = 0;

  if (pcavtk and myid==0) {

    if (ocount==0) {		// Look for restart position.  This is
      while (1) {		// time consuming but is only done once.
	std::ostringstream fileN;
	fileN << runtag << "_pca_" << cC->id << "_" << cC->name
	      << "_" << std::setfill('0') << std::setw(5) << ocount;
	std::ifstream infile(fileN.str());
	if (not infile.good()) break;
	ocount++;
      }
      if (ocount)
	std::cout << "Restart in AxisymmetricBasis::pca_hall: "
		  << "vtk output will begin at "
		  << ocount << std::endl;
    }

    if (compute and ocount % vtkfreq==0) {
      vtkpca = VtkPCAptr(new VtkPCA(nmax));
    }
  }

  if (dof==3)
    Ldim = Lmax*(Lmax + 2) + 1;
  else
    Ldim = 2*Lmax + 1;
    
  if (dof==3) {
    L0 = 0;
    fac02 = 16.0*M_PI*M_PI;
  }
  else {
    L0 = Lmax;
    fac02 = 1.0;
  }

  double fac, var, b;
  int loffset, moffset, indx, lm;

				// For PCA jack knife
  Vector evalJK, cumlJK, snrval;
  Vector meanJK;
  Matrix covrJK;
  Matrix evecJK;
  double Tmass = 0.0;

  if (pcajknf) {
    covrJK.setsize(1, nmax, 1, nmax);
    meanJK.setsize(1, nmax);
    evecJK.setsize(1, nmax, 1, nmax);
    for (auto v : massT) Tmass += v;
  }

  for (int l=L0, loffset=0; l<=Lmax; loffset+=(2*l+1), l++) {

    for (int m=0, moffset=0; m<=l; m++) {

      if (dof==3) {
	lm = l;
	indx = loffset+moffset;
      }
      else {
	lm = m;
	indx = moffset;
      }

      if (m==0) {

	if (compute) {
	  
	  for (int n=1; n<=nmax; n++) {
	    b = (cc[indx][n][n]*fac02 - expcoef[indx][n]*expcoef[indx][n]/muse) /
	      (expcoef[indx][n]*expcoef[indx][n]*muse);
	    b_Hall[indx][n] = 1.0/(1.0 + b);
	  }
    
	  for (int n=1; n<=nmax; n++) {
	    for (int nn=n; nn<=nmax; nn++) {
	      covar[n][nn] = (cc[indx][n][nn]*fac02 - expcoef[indx][n]*expcoef[indx][nn]/muse)/muse;
	      if (n!=nn) covar[nn][n] = covar[n][nn];
	    }    
	  }
	  
	  //
	  // Diagonalize the covariance
	  //
#ifdef GHQL
	  eval = covar.Symmetric_Eigenvalues_GHQL(evec[indx]);
#else
	  eval = covar.Symmetric_Eigenvalues(evec[indx]);
#endif
	  Tevec = evec[indx].Transpose();

	  // Orthonormal test
	  //
	  if (false) {
	    double err_off = 0.0, err_on = 0.0;
	    for (int i=1; i<=nmax; i++) {
	      for (int j=1; j<=nmax; j++) {
		double test = 0.0;
		for (int k=1; k<=nmax; k++) {
		  test += Tevec[i][k]*evec[indx][k][j];
		}
		if (i != j) err_off = std::max<double>(fabs(test    ), err_off);
		else        err_on  = std::max<double>(fabs(test-1.0), err_on );
	      }
	    }
	    std::cout << "Max Error (ortho off, on) = " << err_off
		      << ", " << err_on << std::endl;
	  }

	  // Eigen test
	  //
	  if (false) {
	    double err = 0.0;
	    for (int i=1; i<=nmax; i++) {
	      for (int j=1; j<=nmax; j++) {
		double test = 0.0;
		for (int k=1; k<=nmax; k++) {
		  test += evec[indx][i][k]*eval[k]*Tevec[k][j];
		}
		err = std::max<double>(fabs(test-covar[i][j]), err);
	      }
	    }
	    std::cout << "Max Error (eigen) = " << err << std::endl;
	  }

	  if (tk_type == CumulativeCut) {
	    cuml = eval;
	    for (int n=2; n<=nmax; n++) cuml[n] += cuml[n-1];
	    var = cuml[nmax];
	    for (int n=1; n<=nmax; n++) cuml[n] /= var;
	  }

	  if (pcajknf) {
	    covrJK.zero();
	    meanJK.zero();
	    
	    // Compute mean and variance
	    //
	    for (unsigned T=0; T<sampT; T++) {
	      for (int i=1; i<=nmax; i++) {
		meanJK[i] += (*expcoefT[T])[indx][i]/(massT[T]*sampT);
		for (int j=1; j<=nmax; j++)
		  covrJK[i][j] += (*expcoefT[T])[indx][i]/massT[T] * (*expcoefT[T])[indx][j]/massT[T] / sampT;
	      }
	    }

	    for (int i=1; i<=nmax; i++) {
	      for (int j=1; j<=nmax; j++) {
		covrJK[i][j] -= meanJK[i]*meanJK[j];
	      }
	    }
#ifdef GHQL
	    evalJK = covrJK.Symmetric_Eigenvalues_GHQL(evecJK);
#else
	    evalJK = covrJK.Symmetric_Eigenvalues(evecJK);
#endif

	    // Transformation output
	    //
	    if (cof.good()) {
	      cof << "#" << std::endl
		  << "# cos l=" << l << " m=" << m << std::endl
		  << "#" << std::endl;
	      for (int i=1; i<=nmax; i++) {
		for (int j=1; j<=nmax; j++) {
		  cof << std::setw(12) << evecJK.Transpose()[i][j];
		}
		cof << std::endl;
	      }
	    }

	    // Cumulative distribution
	    //
	    cumlJK = evalJK;
	    for (int n=2; n<=nmax; n++) cumlJK[n] += cumlJK[n-1];
	    for (int n=2; n<=nmax; n++) cumlJK[n] /= cumlJK[nmax];

	    // SNR vector
	    //
	    snrval.setsize(cumlJK.getlow(), cumlJK.gethigh());

	    // Recompute Hall coefficients
	    //
	    for (int n=1; n<=nmax; n++) {
	      b = evalJK[n]/(meanJK[n]*meanJK[n]);
	      b = std::max<double>(b, std::numeric_limits<double>::min());
	      b_Hall[indx][n] = 1.0/(1.0 + b);
	      snrval[n] = sqrt(1.0/b);
	    }
	  }

	  if (vtkpca and myid==0) {
	    if (dof==3)
	      vtkpca->Add(meanJK, b_Hall[indx], snrval, evalJK, evecJK.Transpose(), covrJK, l, m, 'c');
	    else
	      vtkpca->Add(meanJK, b_Hall[indx], snrval, evalJK, evecJK.Transpose(), covrJK, m);
	  }
	  
	  if (out) out << endl;
      
	  for (int n=1; n<=nmax; n++) {

	    double fac = 1.0/muse;

	    double dd = 0.0;
	    for (int nn=1; nn<=nmax; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn]*fac;

	    var = eval[n];

	    if (out) {
	      if (dof==3) out << setw(5) << l;
	      out << setw(5)  << m << setw(5) << 'c' << setw(5) << n;
	      if (!pcajknf) {
		if (covar[n][n] > 0.0)
		  out << setw(18) << covar[n][n]
		      << setw(18) << expcoef[indx][n]*fac
		      << setw(18) << fabs(expcoef[indx][n]*fac)/sqrt(covar[n][n]);
		else
		  out << setw(18) << covar[n][n]
		      << setw(18) << expcoef[indx][n]*fac
		      << setw(18) << "***";
		if (var>0.0)
		  out << setw(18) << var
		      << setw(18) << dd
		      << setw(18) << fabs(dd)/sqrt(var);
		else
		  out << setw(18) << var
		      << setw(18) << dd
		      << setw(18) << "***";
	      }
	      if (pcajknf) {
		double jkvar = evalJK[n];
		if (jkvar>0.0)
		  out << setw(18) << jkvar
		      << setw(18) << cumlJK[n]
		      << setw(18) << meanJK[n]
		      << setw(18) << fabs(meanJK[n])/sqrt(jkvar)
		      << setw(18) << b_Hall[indx][n];
		else
		  out << setw(18) << jkvar
		      << setw(18) << cumlJK[n]
		      << setw(18) << meanJK[n]
		      << setw(18) << "***"
		      << setw(18) << "***";
	      }
	      out << endl;
	    }

	    if (tk_type == VarianceCut) {

	      if (tksmooth*var > dd*dd)
		weight[indx][n] = 0.0;
	      else
		weight[indx][n] = 1.0;

	    }
	    else if (tk_type == CumulativeCut) {
	      
	      if (n==1 || cuml[n] <= tkcum)
		weight[indx][n] = 1.0;
	      else
		weight[indx][n] = 0.0;
		
	    }
	    else if (tk_type == VarianceWeighted) {
	      
	      weight[indx][n] = 1.0/(1.0 + var/(dd*dd + 1.0e-14));
		
	    }
	    else
		weight[indx][n] = 1.0;

	    smth[n] = dd * weight[indx][n];
	  }
	  
	} else {
	  Tevec = evec[indx].Transpose();
	  for (int n=1; n<=nmax; n++) {
	    double dd = 0.0;
	    for (int nn=1; nn<=nmax; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn]/muse;
	    smth[n] = dd * weight[indx][n];
	  }
	}
	    
	inv = evec[indx]*smth;
	for (int n=1; n<=nmax; n++) {
	  if (tk_type != Null) expcoef[indx][n] = inv[n]*muse;
	  if (tk_type == Hall) expcoef[indx][n] *= b_Hall[indx][n];
	}
	
	moffset++;
	
      } else {			// m != 0

	if (compute) {

	  for(int n=1; n<=nmax; n++) {
	    b = (cc[indx][n][n]*fac02 - expcoef[indx][n]*expcoef[indx][n]/muse) / (expcoef[indx][n]*expcoef[indx][n]/muse);
	    b_Hall[indx][n] = 1.0/(1.0 + b);
	  }
    
	  for (int n=1; n<=nmax; n++) {
	    for (int nn=n; nn<=nmax; nn++) {
	      covar[n][nn] = (cc[indx][n][nn]*fac02 - expcoef[indx][n]*expcoef[indx][nn]/muse) / muse;
	      if (n!=nn) covar[nn][n] = covar[n][nn];
	    }
	  }  

	  //
	  // Diagonalize the covariance
	  //
#ifdef GHQL
	  eval = covar.Symmetric_Eigenvalues_GHQL(evec[indx]);
#else
	  eval = covar.Symmetric_Eigenvalues(evec[indx]);
#endif
	  Tevec = evec[indx].Transpose();

	  if (tk_type == CumulativeCut) {
	    cuml = eval;
	    for (int n=2; n<=nmax; n++) cuml[n] += cuml[n-1];
	    var = cuml[nmax];
	    for (int n=1; n<=nmax; n++) cuml[n] /= var;
	  }

	  if (pcajknf) {
	    covrJK.zero();
	    meanJK.zero();
	    
	    // Compute mean and variance
	    //
	    for (unsigned T=0; T<sampT; T++) {
	      for (int i=1; i<=nmax; i++) {
		meanJK[i] += (*expcoefT[T])[indx][i]/(massT[T]*sampT);
		for (int j=1; j<=nmax; j++)
		  covrJK[i][j] += (*expcoefT[T])[indx][i]/massT[T] * (*expcoefT[T])[indx][j]/massT[T] / sampT;
	      }
	    }

	    for (int i=1; i<=nmax; i++) {
	      for (int j=1; j<=nmax; j++) {
		covrJK[i][j] -= meanJK[i]*meanJK[j];
	      }
	    }
#ifdef GHQL
	    evalJK = covrJK.Symmetric_Eigenvalues_GHQL(evecJK);
#else
	    evalJK = covrJK.Symmetric_Eigenvalues(evecJK);
#endif

	    // Transformation output
	    //
	    if (cof.good()) {
	      cof << "#" << std::endl
		  << "# cos l=" << l << " m=" << m << std::endl
		  << "#" << std::endl;
	      for (int i=1; i<=nmax; i++) {
		for (int j=1; j<=nmax; j++) {
		  cof << std::setw(12) << evecJK.Transpose()[i][j];
		}
		cof << std::endl;
	      }
	    }

	    // Cumulative distribution
	    //
	    cumlJK = evalJK;
	    for (int n=2; n<=nmax; n++) cumlJK[n] += cumlJK[n-1];
	    for (int n=2; n<=nmax; n++) cumlJK[n] /= cumlJK[nmax];

	    // SNR vector
	    //
	    snrval.setsize(cumlJK.getlow(), cumlJK.gethigh());

	    // Recompute Hall coefficients
	    //
	    for (int n=1; n<=nmax; n++) {
	      b = evalJK[n]/(meanJK[n]*meanJK[n]);
	      b = std::max<double>(b, std::numeric_limits<double>::min());
	      b_Hall[indx][n] = 1.0/(1.0 + b);
	      snrval[n] = sqrt(1.0/b);
	    }
	  }

	  if (vtkpca and myid==0) {
	    if (dof==3)
	      vtkpca->Add(meanJK, b_Hall[indx], snrval, evalJK, evecJK.Transpose(), covrJK, l, m, 'c');
	    else
	      vtkpca->Add(meanJK, b_Hall[indx], snrval, evalJK, evecJK.Transpose(), covrJK, m);
	  }

	  if (out) out << endl;

	  for (int n=1; n<=nmax; n++) {
	    
	    double dd = 0.0;
	    for (int nn=1; nn<=nmax; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn]/muse;

	    var = eval[n];

	    if (out) {
	      if (dof==3) out << setw(5) << l;
	      out << setw(5)  << m << setw(5) << 'c' << setw(5) << n;
	      if (!pcajknf) {
		if (covar[n][n] > 0.0)
		  out << setw(18) << sqrt(covar[n][n])
		      << setw(18) << expcoef[indx][n]/muse
		      << setw(18) << fabs(expcoef[indx][n]/muse)/sqrt(covar[n][n]);
		else
		  out << setw(18) << covar[n][n]
		      << setw(18) << expcoef[indx][n]/muse
		      << setw(18) << "***";
		if (var>0.0)
		  out << setw(18) << sqrt(var)
		      << setw(18) << dd
		      << setw(18) << fabs(dd)/sqrt(var);
		else
		  out << setw(18) << var
		      << setw(18) << dd
		      << setw(18) << "***";
	      }
	      if (pcajknf) {
		double jkvar = evalJK[n];
		if (jkvar>0.0)
		  out << setw(18) << jkvar
		      << setw(18) << cumlJK[n]
		      << setw(18) << meanJK[n]
		      << setw(18) << fabs(meanJK[n])/sqrt(jkvar)
		      << setw(18) << b_Hall[indx][n];
		else
		  out << setw(18) << jkvar
		      << setw(18) << cumlJK[n]
		      << setw(18) << meanJK[n]
		      << setw(18) << "***"
		      << setw(18) << "***";
	      }
	      out << endl;
	    }

	    if (tk_type == VarianceCut) {

	      if (tksmooth*var > dd*dd)
		weight[indx][n] = 0.0;
	      else
		weight[indx][n] = 1.0;

	    }
	    else if (tk_type == CumulativeCut) {
	      
	      if (n==1 || cuml[n] <= tkcum)
		weight[indx][n] = 1.0;
	      else
		weight[indx][n] = 0.0;
		
	    }
	    else if (tk_type == VarianceWeighted) {
	      
	      weight[indx][n] = 1.0/(1.0 + var/(dd*dd + 1.0e-14));
		
	    }
	    else
		weight[indx][n] = 1.0;

	    smth[n] = dd * weight[indx][n];
	  }

	} else {
	  Tevec = evec[indx].Transpose();
	  for (int n=1; n<=nmax; n++) {
	    double dd = 0.0;
	    for (int nn=1; nn<=nmax; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn]/muse;
	    smth[n] = dd * weight[indx][n];
	  }
	}
	    
	inv = evec[indx]*smth;
	for (int n=1; n<=nmax; n++) {
	  expcoef[indx][n] = inv[n]*muse;
	  if (tk_type == Hall) expcoef[indx][n] *= b_Hall[indx][n];
	}
	
	indx++;

	if (compute) {

	  for (int n=1; n<=nmax; n++) {
	    b = (cc[indx][n][n]*fac02 - expcoef[indx][n]*expcoef[indx][n]/muse) /
	      (expcoef[indx][n]*expcoef[indx][n]*muse);
	    b_Hall[indx][n] = 1.0/(1.0 + b);
	  }
    
	  for (int n=1; n<=nmax; n++) {
	    for (int nn=n; nn<=nmax; nn++) {
	      covar[n][nn] = (cc[indx][n][nn]*fac02 - expcoef[indx][n]*expcoef[indx][nn]/muse) / muse;
	      if (n!=nn) covar[nn][n] = covar[n][nn];
	    }    
	  }

	  //
	  // Diagonalize the covariance
	  //
#ifdef GHQL
	  eval = covar.Symmetric_Eigenvalues_GHQL(evec[indx]);
#else
	  eval = covar.Symmetric_Eigenvalues(evec[indx]);
#endif
	  Tevec = evec[indx].Transpose();

	  if (tk_type == CumulativeCut) {
	    cuml = eval;
	    for (int n=2; n<=nmax; n++) cuml[n] += cuml[n-1];
	    var = cuml[nmax];
	    for (int n=1; n<=nmax; n++) cuml[n] /= var;
	  }

	  if (pcajknf) {
	    covrJK.zero();
	    meanJK.zero();
	    
	    // Compute mean and variance
	    //
	    for (unsigned T=0; T<sampT; T++) {
	      for (int i=1; i<=nmax; i++) {
		meanJK[i] += (*expcoefT[T])[indx][i]/(massT[T]*sampT);
		for (int j=1; j<=nmax; j++)
		  covrJK[i][j] += (*expcoefT[T])[indx][i]/massT[T] * (*expcoefT[T])[indx][j]/massT[T] / sampT;
	      }
	    }

	    for (int i=1; i<=nmax; i++) {
	      for (int j=1; j<=nmax; j++) {
		covrJK[i][j] -= meanJK[i]*meanJK[j];
	      }
	    }
#ifdef GHQL
	    evalJK = covrJK.Symmetric_Eigenvalues_GHQL(evecJK);
#else
	    evalJK = covrJK.Symmetric_Eigenvalues(evecJK);
#endif

	    // Transformation output
	    //
	    if (cof.good()) {
	      cof << "#" << std::endl
		  << "# sin l=" << l << " m=" << m << std::endl
		  << "#" << std::endl;
	      for (int i=1; i<=nmax; i++) {
		for (int j=1; j<=nmax; j++) {
		  cof << std::setw(12) << evecJK.Transpose()[i][j];
		}
		cof << std::endl;
	      }
	    }

	    // Cumulative distribution
	    //
	    cumlJK = evalJK;
	    for (int n=2; n<=nmax; n++) cumlJK[n] += cumlJK[n-1];
	    for (int n=2; n<=nmax; n++) cumlJK[n] /= cumlJK[nmax];

	    // SNR vector
	    //
	    snrval.setsize(cumlJK.getlow(), cumlJK.gethigh());

	    // Recompute Hall coefficients
	    //
	    for (int n=1; n<=nmax; n++) {
	      b = evalJK[n]/(meanJK[n]*meanJK[n]);
	      b = std::max<double>(b, std::numeric_limits<double>::min());
	      b_Hall[indx][n] = 1.0/(1.0 + b);
	      snrval[n] = sqrt(1.0/b);
	    }
	  }

	  if (vtkpca and myid==0) {
	    if (dof==3)
	      vtkpca->Add(meanJK, b_Hall[indx], snrval, evalJK, evecJK.Transpose(), covrJK, l, m, 's');
	    else
	      vtkpca->Add(meanJK, b_Hall[indx], snrval, evalJK, evecJK.Transpose(), covrJK, m);
	  }
	  

	  if (out) out << endl;

	  for (int n=1; n<=nmax; n++) {

	    double dd = 0.0;
	    for (int nn=1; nn<=nmax; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn] / muse;

	    var = eval[n];

	    if (out) {
	      if (dof==3) out << setw(5) << l;
	      out << setw(5)  << m << setw(5) << 's' << setw(5) << n;
	      if (!pcajknf) {
		if (covar[n][n] > 0.0)
		  out << setw(18) << sqrt(covar[n][n])
		      << setw(18) << expcoef[indx][n]/muse
		      << setw(18) << fabs(expcoef[indx][n]/muse)/sqrt(covar[n][n]);
		else
		  out << setw(18) << covar[n][n]
		      << setw(18) << expcoef[indx][n]/muse
		      << setw(18) << "***";
		if (var>0.0)
		  out << setw(18) << sqrt(var)
		      << setw(18) << dd
		      << setw(18) << fabs(dd)/sqrt(var);
		else
		  out << setw(18) << var
		      << setw(18) << dd
		      << setw(18) << "***";
	      }
	      if (pcajknf) {
		double jkvar = evalJK[n];
		if (jkvar>0.0)
		  out << setw(18) << jkvar
		      << setw(18) << cumlJK[n]
		      << setw(18) << meanJK[n]
		      << setw(18) << fabs(meanJK[n])/sqrt(jkvar)
		      << setw(18) << b_Hall[indx][n];
		else
		  out << setw(18) << jkvar
		      << setw(18) << cumlJK[n]
		      << setw(18) << meanJK[n]
		      << setw(18) << "***"
		      << setw(18) << "***";
	      }
	      out << endl;
	    }

	    if (tk_type == VarianceCut) {

	      if (tksmooth*var > dd*dd)
		weight[indx][n] = 0.0;
	      else
		weight[indx][n] = 1.0;

	    }
	    else if (tk_type == CumulativeCut) {
	      
	      if (n==1 || cuml[n] <= tkcum)
		weight[indx][n] = 1.0;
	      else
		weight[indx][n] = 0.0;
		
	    }
	    else if (tk_type == VarianceWeighted) {
	      
	      weight[indx][n] = 1.0/(1.0 + var/(dd*dd + 1.0e-14));
		
	    }
	    else
		weight[indx][n] = 1.0;

	    smth[n] = dd * weight[indx][n];
	  }
	}
	else {
	  Tevec = evec[indx].Transpose();
	  for (int n=1; n<=nmax; n++) {
	    double dd = 0.0;
	    for (int nn=1; nn<=nmax; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn]/muse;
	    smth[n] = dd * weight[indx][n];
	  }
	}

	inv = evec[indx]*smth;
	for (int n=1; n<=nmax; n++) {
	  if (tk_type != Null) expcoef[indx][n] = inv[n]*muse;
	  if (tk_type == Hall) expcoef[indx][n] *= b_Hall[indx][n];
	}
	
	moffset += 2;
      }
    }
  }


  if (vtkpca) {
    std::ostringstream sout;

    sout << runtag << "_pca_" << cC->id << "_" << cC->name
	 << "_" << std::setfill('0') << std::setw(5) << ocount++;
    vtkpca->Write(sout.str());
  }

}

void AxisymmetricBasis::parallel_gather_coefficients(void)
{

  if (myid == 0) {

    for (int l=L0, loffset=0; l<=Lmax; loffset+=(2*l+1), l++) {

      for (int m=0, moffset=0; m<=l; m++) {

	if (m==0) {
	  for (int n=1; n<=nmax; ++n) {
	    expcoef[loffset+moffset][n] = 0.0;
	  }
	  moffset++;

	} else {
	  for (int n=1; n<=nmax; ++n) {
	    expcoef[loffset+moffset][n] = 0.0;
	    expcoef[loffset+moffset+1][n] = 0.0;
	  }
	  moffset+=2;
	}
      }
    }
  }


  for (int l=L0, loffset=0; l<=Lmax; loffset+=(2*l+1), l++) {

    for (int m=0, moffset=0; m<=l; m++) {

      if (m==0) {
	MPI_Reduce(&expcoef1[loffset+moffset][1], 
		   &expcoef [loffset+moffset][1], nmax, 
		   MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	moffset++;
      }
      else {
	MPI_Reduce(&expcoef1[loffset+moffset][1], 
		   &expcoef [loffset+moffset][1], nmax, 
		   MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&expcoef1[loffset+moffset+1][1],
		   &expcoef [loffset+moffset+1][1], nmax, 
		   MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	
	moffset+=2;
      }
    }
  }

}

void AxisymmetricBasis::parallel_distribute_coefficients(void)
{

  for (int l=L0, loffset=0; l<=Lmax; loffset+=(2*l+1), l++) {

      for (int m=0, moffset=0; m<=l; m++) {

	if (m==0) {
	  MPI_Bcast(&expcoef[loffset+moffset][1], nmax, MPI_DOUBLE,
		    0, MPI_COMM_WORLD);
	  moffset++;
	}
	else {
	  MPI_Bcast(&expcoef[loffset+moffset][1], nmax, MPI_DOUBLE,
		     0, MPI_COMM_WORLD);
	  MPI_Bcast(&expcoef[loffset+moffset+1][1], nmax, MPI_DOUBLE,
		    0, MPI_COMM_WORLD);
	  moffset+=2;
	}
      }
  }

}


void AxisymmetricBasis::parallel_gather_coef2(void)
{

  for (int l=L0, loffset=0; l<=Lmax; loffset+=(2*l+1), l++) {

    for (int m=0, moffset=0; m<=l; m++) {

      if (m==0) {
	for (int n=1; n<=nmax; n++)
	  MPI_Allreduce(&cc1[loffset+moffset][n][n],
			&cc [loffset+moffset][n][n], nmax-n+1, 
			MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	moffset++;
      }
      else {
	for (int n=1; n<=nmax; n++) {
	  MPI_Allreduce(&cc1[loffset+moffset  ][n][n],
			&cc [loffset+moffset  ][n][n], nmax-n+1, 
			MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	  MPI_Allreduce(&cc1[loffset+moffset+1][n][n],
			&cc [loffset+moffset+1][n][n], nmax-n+1, 
			MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	}

	moffset+=2;
      }
    }
  }

  for (int l=L0, loffset=0; l<=Lmax; loffset+=(2*l+1), l++) {

    for (int m=0, moffset=0; m<=l; m++) {

      if (m==0) {
	for (int n=1; n<=nmax; ++n) {
	  for (int nn=n+1; nn<=nmax; nn++)
	    cc[loffset+moffset][nn][n] = cc[loffset+moffset][n][nn];
	}
	moffset++;
	
      } else {
	for (int n=1; n<=nmax; ++n) {
	  for (int nn=n+1; nn<=nmax; nn++) {
	    cc[loffset+moffset  ][nn][n] = cc[loffset+moffset  ][n][nn];
	    cc[loffset+moffset+1][nn][n] = cc[loffset+moffset+1][n][nn];
	  }
	}
	moffset+=2;
      }
    }
  }

  if (pcajknf) {

    MPI_Allreduce(&massT1[0], &massT[0], sampT,
		  MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    for (unsigned T=0; T<sampT; T++) {
      for (int l=0; l<=Lmax*(Lmax+2); l++) {
	MPI_Allreduce(&(*expcoefT1[T])[l][1],
		      &(*expcoefT [T])[l][1], nmax,
		      MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      }
    }
  }

}

AxisymmetricBasis::TKType AxisymmetricBasis::setTK(const std::string& tk)
{
  TKType ret = Null;

  if      (tk == "Hall")             ret = Hall;
  else if (tk == "VarianceCut")      ret = VarianceCut;
  else if (tk == "CumulativeCut")    ret = CumulativeCut;
  else if (tk == "VarianceWeighted") ret = VarianceWeighted;
  else if (tk == "Null")             ret = Null;
  else {
    if (myid==0) {
      cout << "AxisymmetricBasis: no such TK type <" << tk << ">"
	   << " using Null type\n";
    }
  }
}
