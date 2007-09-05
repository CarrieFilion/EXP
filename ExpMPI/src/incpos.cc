/*
  First part of leap-frog: step in position
*/

#include "expand.h"

#ifdef RCSID
static char rcsid[] = "$Id$";
#endif

void * incr_position_thread(void *ptr)
{
  // Current time step
  //
  double dt = static_cast<thrd_pass_posvel*>(ptr)->dt;

  // Current level
  //
  int mlevel = static_cast<thrd_pass_posvel*>(ptr)->mlevel;

  // Thread ID
  //
  int id = static_cast<thrd_pass_posvel*>(ptr)->id;

  
  list<Component*>::iterator cc;
  int nbeg, nend;
  Component *c;
  
  //
  // Component loop
  //
  for (cc=comp.components.begin(); cc != comp.components.end(); cc++) {
    c = *cc;

    unsigned ntot = c->Number();

    //
    // Compute the beginning and end points in particle list
    // for each thread
    //
    nbeg = ntot*id    /nthrds;
    nend = ntot*(id+1)/nthrds;

    for (unsigned i=nbeg; i<nend; i++) {
				// If we are multistepping, only 
				// advance for this level
      if (multistep && mlevel>=0 && (c->Part(i)->level != mlevel)) continue;

      for (int k=0; k<c->dim; k++) 
	c->Part(i)->pos[k] += (c->Part(i)->vel[k] - c->covI[k])*dt;
    }
    
  }

}


void incr_position(double dt, int mlevel)
{
  if (!eqmotion) return;

  //
  // Make the <nthrds> threads
  //
  int errcode;
  void *retval;
  
  for (int i=0; i<nthrds; i++) {

    posvel_data[i].dt = dt;
    posvel_data[i].mlevel = mlevel;
    posvel_data[i].id = i;

    errcode =  pthread_create(&posvel_thrd[i], 0, incr_position_thread, 
			      &posvel_data[i]);

    if (errcode) {
      cerr << "Process " << myid
	   << " incr_position: cannot make thread " << i
	   << ", errcode=" << errcode << endl;
      exit(19);
    }
#ifdef DEBUG
    else {
      cout << "Process " << myid << ": thread <" << i << "> created\n";
    }
#endif
  }
    
  //
  // Collapse the threads
  //
  for (int i=0; i<nthrds; i++) {
    if ((errcode=pthread_join(posvel_thrd[i], &retval))) {
      cerr << "Process " << myid
	   << " incr_position: thread join " << i
	   << " failed, errcode=" << errcode << endl;
      exit(20);
    }
#ifdef DEBUG    
    cout << "Process " << myid << ": incr_position thread <" 
	 << i << "> thread exited\n";
#endif
  }
  

  //
  // Only do this once per multistep
  //
  if (multistep==0 || (mstep==Mstep && mlevel==multistep)) {

    list<Component*>::iterator cc;
    Component *c;
  
    for (cc=comp.components.begin(); cc != comp.components.end(); cc++) {
      c = *cc;

      if (c->com_system) {
	for (int k=0; k<c->dim; k++) c->com0[k] += c->cov0[k]*dt;
      }
    }
    
  }
  
}
