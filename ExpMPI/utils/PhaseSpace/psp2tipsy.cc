/*
  Info on each stanza of a phase space dump

  MDWeinberg 03/17/02
*/

#include <unistd.h>
#include <stdlib.h>

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <list>

#include <StringTok.H>
#include <header.H>
#include <PSP.H>

#define MAXDIM 3

typedef float Real;

struct gas_particle {
    Real mass;
    Real pos[MAXDIM];
    Real vel[MAXDIM];
    Real rho;
    Real temp;
    Real hsmooth;
    Real metals ;
    Real phi ;
} ;

struct dark_particle {
    Real mass;
    Real pos[MAXDIM];
    Real vel[MAXDIM];
    Real eps;
    Real phi ;
} ;

struct star_particle {
    Real mass;
    Real pos[MAXDIM];
    Real vel[MAXDIM];
    Real metals ;
    Real tform ;
    Real eps;
    Real phi ;
} ;

struct tipsydump {
    double time ;
    int nbodies ;
    int ndim ;
    int nsph ;
    int ndark ;
    int nstar ;
} ;

extern string trimLeft(const string);
extern string trimRight(const string);

				// Globals for exputil library
				// Unused here
int myid = 0;
char threading_on = 0;
pthread_mutex_t mem_lock;

//-------------
// Help message
//-------------

void Usage(char* prog) {
  cerr << prog << ": [-t time -v -h] filename\n\n";
  cerr << "    -t time         use dump closest to <time>\n";
  cerr << "    -a              convert entire file\n";
  cerr << "    -h              print this help message\n";
  cerr << "    -v              verbose output\n\n";
  exit(0);
}

void write_tipsy(ifstream *in, PSPDump &psp)
{
				// Create tipsy output
				// -------------------
  tipsydump theader;
  gas_particle gas;
  dark_particle dark;
  star_particle star;

  theader.time = psp.CurrentDump()->header.time;
  theader.nbodies = psp.CurrentDump()->ntot;
  theader.ndim = 3;
  theader.nsph = psp.CurrentDump()->ngas;
  theader.ndark = psp.CurrentDump()->ndark;
  theader.nstar = psp.CurrentDump()->nstar;
  
  cout.write((char *)&theader, sizeof(tipsydump));
    
  double rtmp;
  int itmp;
  
  PSPstanza *stanza;
  SParticle *part;
				// Do gas particles
				// ----------------

  for (stanza=psp.GetGas(); stanza!=0; stanza=psp.NextGas()) {
      
    for (part=psp.GetParticle(in); part!=0; part=psp.NextParticle(in)) {

      gas.mass = part->mass;
      for (int i=0; i<3; i++) gas.pos[i] = part->pos[i];
      for (int i=0; i<3; i++) gas.vel[i] = part->vel[i];
      gas.phi = part->phi;
      gas.rho = gas.temp = gas.hsmooth = gas.metals = 0.0;
      
      cout.write((char *)&gas, sizeof(gas_particle));
    }
  }

				// Do dark particles
				// -----------------
  for (stanza=psp.GetDark(); stanza!=0; stanza=psp.NextDark()) {
      
    for (part=psp.GetParticle(in); part!=0; part=psp.NextParticle(in)) {

      dark.mass = part->mass;
      for (int i=0; i<3; i++) dark.pos[i] = part->pos[i];
      for (int i=0; i<3; i++) dark.vel[i] = part->vel[i];
      dark.phi = part->phi;
      dark.eps = 0.0;
      
      cout.write((char *)&dark, sizeof(dark_particle));
    }
  }
  
				// Do star particles
				// -----------------

  for (stanza=psp.GetStar(); stanza!=0; stanza=psp.NextStar()) {
      
    for (part=psp.GetParticle(in); part!=0; part=psp.NextParticle(in)) {

      star.mass = part->mass;
      for (int i=0; i<3; i++) star.pos[i] = part->pos[i];
      for (int i=0; i<3; i++) star.vel[i] = part->vel[i];
      star.phi = part->phi;
      star.metals = star.tform = star.eps = 0.0;
      
      cout.write((char *)&star, sizeof(star_particle));
    }
  }
  
}

int
main(int argc, char **argv)
{
  char *prog = argv[0];
  double time=1e20;
  bool verbose = false;
  bool all = false;

  // Parse command line

  while (1) {

    int c = getopt(argc, argv, "t:avh");

    if (c == -1) break;

    switch (c) {

    case 't':
      time = atof(optarg);
      break;

    case 'v':
      verbose = true;
      break;

    case 'a':
      all = true;
      break;

    case '?':
    case 'h':
    default:
      Usage(prog);
    }

  }

  ifstream *in;

  if (optind < argc) {

    ifstream *in2 = new ifstream(argv[optind]);
    if (!*in2) {
      cerr << "Error opening file <" << argv[optind] << "> for input\n";
      exit(-1);
    }

    cerr << "Using filename: " << argv[optind] << endl;

				// Assign file stream to input stream
    in = in2;

  }

				// Read the phase space file
				// -------------------------

  PSPDump psp(in, true);

  in->close();
  delete in;
				// Reopen file
  in = new ifstream(argv[optind]);

				// Create tipsy output
				// -------------------

  if (all) {

				// Write a summary
				// -------------------
    if (verbose) psp.PrintSummary(cerr);

    Dump *dump;
    for (dump=psp.GetDump(); dump!=0; dump=psp.NextDump())
      write_tipsy(in, psp);

  } else {

    cerr << "\nBest fit dump to <" << time << "> has time <" 
	 << psp.SetTime(time) << ">\n";
  
				// Write a summary
				// -------------------
    if (verbose) psp.PrintSummaryCurrent(cerr);

    write_tipsy(in, psp);
  }
  
  cerr << "Done\n";

  return 0;
}
  
