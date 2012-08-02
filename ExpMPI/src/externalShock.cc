
#include "expand.h"

#include <stdlib.h>
#include <string>
#include <Vector.h>
#include <numerical.h>

#include <externalShock.H>

externalShock::externalShock(std::string& line) : ExternalForce(line)
{
  E=-0.5;
  K=1.0e-4;
  PER=0.25;
  AMPL=1.0;
  INFILE = "w05";

  initialize();

  model = new SphericalModelTable(INFILE);
  t = new SphericalOrbit(model, E, K);
}


externalShock::~externalShock()
{
  delete t;
  delete model;
}

void externalShock::initialize()
{
  std::string val;

  if (get_value("E", val)) E = atof(val.c_str());
  if (get_value("K", val)) K = atof(val.c_str());
  if (get_value("PER", val)) PER = atof(val.c_str());
  if (get_value("AMPL", val)) AMPL = atof(val.c_str());
  if (get_value("INFILE", val)) INFILE = val;
}


void * externalShock::determine_acceleration_and_potential_thread(void * arg)
{
  unsigned nbodies = cC->Number();
  int id = *((int*)arg);
  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;

  double w2, x, z;

  w2 = get_tidal_shock(tnow);

  PartMapItr it = cC->Particles().begin();
  unsigned long i;


  for (int q=0   ; q<nbeg; q++) it++;
  for (int q=nbeg; q<nend; q++)
    {
      i = (it++)->first;

      x = cC->Pos(i, 0);
      z = cC->Pos(i, 2);

      if (component->freeze(i)) continue;

      cC->AddAcc(i, 2, -w2*x );
      cC->AddPotExt(i, 0.5*w2*z*z );
    }

}


double externalShock::get_tidal_shock(double T)
{
  return AMPL * model->get_dpot2(t->get_angle(6, T*PER));
}


