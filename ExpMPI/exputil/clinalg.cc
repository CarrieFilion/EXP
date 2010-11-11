#include <cstdlib>
#include <cmath>
#include <mpi.h>
#include <Vector.h>
#include <kevin_complex.h>

#include <clinalg.h>

using namespace std;

extern int myid;

void bomb_clinalg(const char *msg)
{
  if (myid>=0) {
        cerr << "clinalg ERROR [mpi_id=" << myid << "]: " << msg << '\n';
	MPI_Abort(MPI_COMM_WORLD, -1);
  }
  else {
    cerr << "clinalg ERROR: " << msg << '\n';
#ifdef DEBUG
    chdir("/tmp");
    abort();
#endif
  }
  exit(0);
}

/* Compute LU decomposition */

#define TINY 1.0e-20
#define TOL 1.0e-12

int lu_decomp(CMatrix& a, int *indx, double& d)
{
  int i,imax,j,k;
  double big, dum, temp;
  int n = a.getnrows();
  KComplex sum, ctemp;

  Vector vv(1,n);
				// No row interchanges yet
  d = 1.0;

				// Loop over rows to get the implicit 
				// scaling function
  for (i=1; i<=n; i++) {
    big = 0.0;
    for (j=1; j<=n; j++)
      if ((temp=fabs(a[i][j])) > big) big=temp;
    if (big < TOL) {
      fputs("Singular matrix in routine lu_decomp\n", stderr);
      return 1;
    }
				// Save the scaling
    vv[i]=1.0/big;
  }
				// Loop over columns
  for (j=1; j<=n; j++) {
    for (i=1; i<j; i++) {
      sum = a[i][j];
      for (k=1; k<i; k++)
	sum -= a[i][k]*a[k][j];
      a[i][j] = sum;
    }
				// Search for the largest pivot element
    big=0.0;
    for (i=j; i<=n; i++) {
      sum = a[i][j];
      for (k=1; k<j; k++)
	sum -= a[i][k]*a[k][j];
      a[i][j] = sum;
				// Figure of merit for pivot . . .
				// Better than the best so far?
      if ( (dum=vv[i]*fabs(sum)) >= big) {
	big = dum;
	imax = i;
      }
    }
				// Need to interchange rows?
    if (j != imax) {
      for (k=1; k<=n; k++) {
	ctemp = a[imax][k];
	a[imax][k] = a[j][k];
	a[j][k] = ctemp;
      }
				// Change parity of d and interchange the
				// scale factor
      d = -d;
      vv[imax] = vv[j];
    }
    indx[j] = imax;
    if (fabs(a[j][j]) == 0.0) a[j][j] = TINY;
    if (j != n) {
				// Divide by the pivot element
      ctemp = 1.0/a[j][j];
      for (i=j+1; i<=n; i++) a[i][j] *= ctemp;
    }
  }
				// Successful
  return 0;
}

#undef TINY
#undef TOL


/*
	Solves the set of linear equations A * X = B.  Here a is input,
	not as the matrix A but rather its LU decomposition, determined
	by the routine lu_decomp.  indx is input as the permutation vector
	returned by lu_decomp.  b is input as the right-hand side vector
	B, and returns with the solution vector X.  a, n, and indx are not
	modified by this routine and can be left in place for successive calls
	with different right-hand sides b.  This routine takes into account
	the possibility that b will begin with many zero elements, so it
	is efficient for use in matrix inversion.
*/

void lu_backsub(CMatrix& a, int* indx, CVector& b)
{
  int i,ip,j;
//  int ii=0;
  int n = a.getnrows();
  KComplex sum;

  for (i=1; i<=n; i++) {
    ip = indx[i];
    sum = b[ip];
    b[ip] = b[i];

/*    if (ii)
      for (j=ii; j<=i-1; j++) sum -= a[i][j]*b[j];
    else if (fabs(sum)==0.0) ii = i; */

    for (j=1; j<i; j++) sum -= a[i][j] * b[j];
    b[i] = sum;
  }
  for (i=n; i>=1; i--) {
    sum = b[i];
    for (j=i+1; j<=n; j++) sum -= a[i][j]*b[j];
    b[i] = sum/a[i][i];
  }
}


int linear_solve(CMatrix& a, CVector& b, CVector& x)

/* Solves an nth-order linear system of equations a x = b using
	LU decomposition.       a, b, and x are previously allocated as
	a[n][n], b[n] and x[n].  Only x is modified in the routine. */

{
  int *index;

// Make copies of a and b so as not to disturb their contents

  int n = a.getnrows();
  CMatrix m = a;
  x = b;

// Perform LU decomposition and back-substitution

  double d;
  index = new int[n] - 1;
  if (lu_decomp(m, index, d)==1)
    {
      delete [] (index+1);
      return 1;
    }
  lu_backsub(m, index, x);

  delete [] (index+1);
  return 0;
}


/* Finds the inverse of a and places the result in b.  It is stored in
	a temporary in the meantime so that a and b can be the same matrix.  */

int inverse(CMatrix& a, CMatrix& b)
{
  double d;
  int i;
  
// Make a copy of a so as not to disturb its contents

  int n = a.getnrows();
  CMatrix m = a;
  CMatrix m2(1, n, 1, n);   m2.zero();

// Perform LU decomposition and successive back-substitution

  int* index = new int[n] - 1;
  if (lu_decomp(m, index, d)==1) {
    delete [] (index+1);
    return 1;
  }

  for (i=1; i<=n; i++) {
    m2[i][i] = 1.0;
    lu_backsub(m, index, m2.row(i));
  }

  delete [] (index+1);
				// assign result to b
  b = m2.Transpose();

  return 0;
}


KComplex lu_determinant(CMatrix& a, double& d)
{
  int i, n=a.getnrows();
  KComplex det;

  det = d;
  for (i=1; i<=n; i++)
    det *= a[i][i];
  
  return det;
}


     
KComplex determinant(CMatrix& a)
{
  int n = a.getnrows();

  if (n!= a.getncols()) {
    fputs("Can not take determinant of nonsquare matrix.\n", stderr);
    return 0.0;
  }


  CMatrix m = a;
  int* index = new int[n] - 1;
  double d;

  int iret = lu_decomp(m, index, d);
  delete [] (index+1);

  if (iret) return 0.0;

  KComplex det = d;
  for (int i=1; i<=n; i++)
    det *= m[i][i];
  
  return det;
}




CMatrix sub_matrix(CMatrix& in, 
		   int ibeg, int iend, int jbeg, int jend, int ioff, int joff)
{
  CMatrix out;

  if ( ibeg<in.getrlow() || iend>in.getrhigh() || jbeg<in.getclow() ||
       jend>in.getchigh() ) {
    bomb_clinalg("Error in sub_matrix input");
  }

  out.setsize(ibeg+ioff, iend+ioff, jbeg+joff, jend+joff);

  for (int i=ibeg; i<=iend; i++) {
    for (int j=jbeg; j<=jend; j++)
      out[i+ioff][j+joff] = in[i][j];
  }

  return out;

}


void embed_matrix(CMatrix& to, CMatrix& from, int rbeg, int cbeg)
{
  CMatrix out;

  int from_row_size = from.getrhigh() - from.getrlow() + 1;
  int from_col_size = from.getchigh() - from.getclow() + 1;

  if ( rbeg<to.getrlow() || cbeg<to.getclow() || 
      from_row_size + rbeg -1 > to.getrhigh() ||
      from_col_size + cbeg -1 > to.getrhigh() ) {
    bomb_clinalg("Error in embed_matrix input (sizes!)");
  }

  for (int i=rbeg; i<rbeg+from_row_size; i++) {
    for (int j=cbeg; j<cbeg+from_col_size; j++)
      to[i][j] = from[i-rbeg+from.getrlow()][j-cbeg+from.getclow()];
  }
}

void embed_matrix(Matrix& to, Matrix& from, int rbeg, int cbeg)
{
  CMatrix out;

  int from_row_size = from.getrhigh() - from.getrlow() + 1;
  int from_col_size = from.getchigh() - from.getclow() + 1;

  if ( rbeg<to.getrlow() || cbeg<to.getclow() || 
      from_row_size + rbeg -1 > to.getrhigh() ||
      from_col_size + cbeg -1 > to.getrhigh() ) {
    bomb_clinalg("Error in inbed_matrix input (sizes!)");
  }

  for (int i=rbeg; i<rbeg+from_row_size; i++) {
    for (int j=cbeg; j<cbeg+from_col_size; j++)
      to[i][j] = from[i-rbeg+from.getrlow()][j-cbeg+from.getclow()];
  }
}

void embed_matrix(CMatrix& to, Matrix& from, int rbeg, int cbeg)
{
  CMatrix out;

  int from_row_size = from.getrhigh() - from.getrlow() + 1;
  int from_col_size = from.getchigh() - from.getclow() + 1;

  if ( rbeg<to.getrlow() || cbeg<to.getclow() || 
      from_row_size + rbeg -1 > to.getrhigh() ||
      from_col_size + cbeg -1 > to.getrhigh() ) {
    bomb_clinalg("Error in inbed_matrix input (sizes!)");
  }

  for (int i=rbeg; i<rbeg+from_row_size; i++) {
    for (int j=cbeg; j<cbeg+from_col_size; j++)
      to[i][j] = from[i-rbeg+from.getrlow()][j-cbeg+from.getclow()];
  }
}

