/*****************************************************************************
 *  Description:
 *  -----------
 *
 *  Return Euler angle matrix (ref. Goldstein)
 *
 *  Call sequence:
 *  -------------
 *  Eigen::Matrix3d return_euler(double PHI, double THETA, double PSI, int BODY);
 *
 *  Euler angles: PHI, THETA, PSI
 *
 *  BODY = 0:     rotate axes, keep vector (or "body") fixed in space
 *  BODY = 1:     rotate body
 *
 *
 *  Parameters:
 *  ----------
 *
 *  TEST -- compile with main() test routine
 *
 *  Returns:
 *  -------
 *
 *  Euler transformation
 *
 *  Notes:
 *  -----
 *
 *
 *  By:
 *  --
 *
 *  MDW 03/29/93
 *
 ***************************************************************************/

#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cmath>

#include <Eigen/Eigen>

// #define TEST for test program

Eigen::Matrix3d return_euler(double PHI, double THETA, double PSI, int BODY);


Eigen::Matrix3d return_euler(double PHI, double THETA, double PSI, int BODY)
{
  double sph, cph, sth, cth, sps, cps;

  Eigen::MatrixXd euler(3, 3);

  sph = sin(PHI);
  cph = cos(PHI);
  sth = sin(THETA);
  cth = cos(THETA);
  sps = sin(PSI);
  cps = cos(PSI);
  
  if (BODY) {

    euler(0, 0) =  cps*cph - cth*sph*sps;
    euler(1, 0) =  cps*sph + cth*cph*sps;
    euler(2, 0) =  sps*sth;
    
    euler(0, 1) = -sps*cph - cth*sph*cps;
    euler(1, 1) = -sps*sph + cth*cph*cps;
    euler(2, 1) =  cps*sth;
  
    euler(0, 2) =  sth*sph;
    euler(1, 2) = -sth*cph;
    euler(2, 2) =  cth;

  }
  else {
    
    euler(0, 0) =  cps*cph - cth*sph*sps;
    euler(0, 1) =  cps*sph + cth*cph*sps;
    euler(0, 2) =  sps*sth;
      
    euler(1, 0) = -sps*cph - cth*sph*cps;
    euler(1, 1) = -sps*sph + cth*cph*cps;
    euler(1, 2) =  cps*sth;
      
    euler(2, 0) =  sth*sph;
    euler(2, 1) = -sth*cph;
    euler(2, 2) =  cth;
    
  }

  return euler;
}

#ifdef TEST

int
main(int argc, char **argv)
{
  double phi, theta, psi;

  std::cout << "Phi, Theta, Psi: ";
  std::cin >> phi;
  std::cin >> theta;
  std::cin >> psi;

  auto euler0 = return_euler(phi, theta, psi, 0);
  auto euler1 = return_euler(phi, theta, psi, 1);

  std::cout << std::endl << euler0 << std::endl;
  std::cout << std::endl << euler1 << std::endl;

  auto eulert = euler0*euler1;
  std::cout << std::endl << eulert << std::endl;

  return 0;
}

#endif // TEST
