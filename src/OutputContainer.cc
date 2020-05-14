
#include "expand.h"

#include <OutputContainer.H>

#include <OutLog.H>
#include <OrbTrace.H>
#include <OutDiag.H>
#include <OutPS.H>
#include <OutPSN.H>
#include <OutPSP.H>
#include <OutPSQ.H>
#include <OutAscii.H>
#include <OutCHKPT.H>
#include <OutCHKPTQ.H>
#include <OutCoef.H>
#include <OutFrac.H>
#include <OutCalbr.H>
#include <OutMulti.H>

OutputContainer::OutputContainer()
{
  // Mark time ahead of current time on restart
  //
  if (restart) last = tnow + 0.6*dtime;
  //
  // and behind current time on restart
  //
  else         last = tnow - 0.6*dtime;
}

void OutputContainer::initialize(void)
{
  YAML::Node outs = parse["Output"];

  if (outs.IsSequence()) {

    int nout = 0;

    while (outs[nout]) {
      std::string name = outs[nout]["id"].as<std::string>();
      const YAML::Node& node = outs[nout]["parameters"];
    
      if ( !name.compare("outlog") ) {
	out.push_back(new OutLog(node));
      }
      
      else if ( !name.compare("orbtrace") ) {
	out.push_back(new OrbTrace(node));
      }
      
      else if ( !name.compare("outdiag") ) {
	out.push_back(new OutDiag(node));
      }

      else if ( !name.compare("outps") ) {
	out.push_back(new OutPS(node));
      }

      else if ( !name.compare("outpsn") ) {
	out.push_back(new OutPSN(node));
      }
    
      else if ( !name.compare("outpsp") ) {
	out.push_back(new OutPSP(node));
      }
    
      else if ( !name.compare("outpsq") ) {
	out.push_back(new OutPSQ (node));
      }
    
      else if ( !name.compare("outascii") ) {
	out.push_back(new OutAscii(node));
      }
    
      else if ( !name.compare("outchkpt") ) {
	out.push_back(new OutCHKPT(node));
      }

      else if ( !name.compare("outchkptq") ) {
	out.push_back(new OutCHKPTQ(node));
      }

      else if ( !name.compare("outcoef") ) {
	out.push_back(new OutCoef(node));
      }

      else if ( !name.compare("outfrac") ) {
	out.push_back(new OutFrac(node));
      }

      else if ( !name.compare("outmulti") ) {
	out.push_back(new OutMulti(node));
      }

      else if ( !name.compare("outcalbr") ) {
	out.push_back(new OutCalbr(node));
      }
      
      else {
	string msg("I don't know about the output type: ");
	msg += name;
	throw GenericError(msg, __FILE__, __LINE__);
      }
      nout++;
    }
  } else {
    if (myid==0)
      std::cout << std::string(72, '-') << std::endl
		<< "No output entries" << std::endl
		<< std::string(72, '-') << std::endl;
  }
}

  
OutputContainer::~OutputContainer()
{
  // Delete all Output instances
  //
  for (auto it : out) delete it;
}

void OutputContainer::Run(int nstep, int mstep, bool final)
{
  // Don't rerun a step . . . but allow for multisteps to be run
  //
  if (fabs(tnow - last) < 0.5*dtime/Mstep) return;

  // Loop through all instances
  //
  for (auto it : out) it->Run(nstep, mstep, final);

  // Root node output
  //
  if (myid==0 and mstep==0) {
#ifdef DEBUG
    cout << setw(60) << setfill('=') << "=" << endl
	 << "====== Step " << nstep << "/" << mstep << endl
	 << setw(60) << setfill('=') << "=" << endl
	 << setfill(' ');
#else
    cout << "." << nstep << flush;
#endif
    if (final) cout << "\n";
  }

  // Mark: step ran at this time
  //
  last = tnow;
}
