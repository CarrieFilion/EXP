#include <mpi.h>
#include <SatFix.H>

SatFix::SatFix(string &line) : ExternalForce(line)
{
  verbose = false;
  comp_name = "Points";		// Default component for fixing

  initialize();

				// Look for the fiducial component
  bool found = false;
  list<Component*>::iterator cc;
  Component *c;
  for (cc=comp.components.begin(); cc != comp.components.end(); cc++) {
    c = *cc;
    if ( !comp_name.compare(c->name) ) {
      c0 = c;
      found = true;
      break;
    }
  }

  // Find out who has particles, make sure that there are an even number
  if (2*(c0->nbodies_tot/2) != c0->nbodies_tot) {
    if (myid==0) cerr << "SatFix: component <" << comp_name 
		      << "> has an odd number of particles!!! nbodies_tot=" 
		      << c0->nbodies_tot << "\n";
    MPI_Abort(MPI_COMM_WORLD, 36);
  }

  if (!found) {
    cerr << "Process " << myid << ": SatFix can't find desired component <"
	 << comp_name << ">" << endl;
    MPI_Abort(MPI_COMM_WORLD, 35);
  }

  last = vector<int>(numprocs, 0);

  userinfo();
}

SatFix::~SatFix()
{
}

void SatFix::userinfo()
{
  if (myid) return;		// Return if node master node

  print_divider();
  cout << "** Enforces mirror coordinates for adjacent particles on component: " 
       << c0->name << endl;
  print_divider();
}

void SatFix::initialize()
{
  string val;

  if (get_value("compname", val))   comp_name = val;
  if (get_value("verbose", val))    if (atoi(val.c_str())) verbose = true;
}

void SatFix::get_acceleration_and_potential(Component* C)
{
  if (C != c0) return;

  compute_list();

  MPL_start_timer();

				// Check for info from previous process
  if (recv[myid]>=0) {
    
    MPI_Recv(C->Part(0)->pos, 3, MPI_DOUBLE, recv[myid], 133, 
	     MPI_COMM_WORLD, &status);
    MPI_Recv(C->Part(0)->vel, 3, MPI_DOUBLE, recv[myid], 134, 
	     MPI_COMM_WORLD, &status);
    MPI_Recv(C->Part(0)->acc, 3, MPI_DOUBLE, recv[myid], 135, 
	     MPI_COMM_WORLD, &status);
    
				// Temporary!  Remove this . . .
#if 0
    print_recv();
#endif
				// Change sign of phase space
    for (int k=0; k<3; k++) {
      C->Part(0)->pos[k] *= -1.0;
      C->Part(0)->vel[k] *= -1.0;
      C->Part(0)->acc[k] *= -1.0;
    }

    check_recv();
  }

				// Send info to next process
  if (send[myid]>=0) {
    
				// Temporary!  Remove this . . .
#if 0
    print_send();
#endif

    check_send();

    MPI_Send(C->Part(end)->pos, 3, MPI_DOUBLE, send[myid], 133, 
	     MPI_COMM_WORLD);
    MPI_Send(C->Part(end)->vel, 3, MPI_DOUBLE, send[myid], 134, 
	     MPI_COMM_WORLD);
    MPI_Send(C->Part(end)->acc, 3, MPI_DOUBLE, send[myid], 135, 
	     MPI_COMM_WORLD);
  }

  if (ncount[myid]) {
    for (int n=begin; n<end; n+=2) {

      check_body(n);

      for (int k=0; k<3; k++) {
	C->Part(n+1)->pos[k] = -C->Part(n)->pos[k];
	C->Part(n+1)->vel[k] = -C->Part(n)->vel[k];
	C->Part(n+1)->acc[k] = -C->Part(n)->acc[k];
      }
    } 
  }

  MPL_stop_timer();
}

void SatFix::compute_list()
{
				// Get body count
  ncount = c0->particle_count();

				// Check for change in body count
  bool recompute = false;
  for (int n=0; n<numprocs; n++) {
    if (last[n] != ncount[n]) recompute = true;
  }
  
  if (recompute) {
				// Deal with end points
    send = vector<int>(numprocs, -1);
    recv = vector<int>(numprocs, -1);
    bool remainder = false;
    int from=0, number;

    for (int n=0; n<numprocs; n++) {
      number = ncount[n];

      if (number>0) {
				// Particle to be sent from previous node
	if (remainder) {
	  send[from] = n;
	  recv[n] = from;
	  number--;
	  remainder = false;
	}
				// Particlde to be sent to next node
	if ( 2*(number/2) != number ) {
	  from = n;
	  remainder = true;
	}
	
      }
    }

    begin = 0;
    end   = ncount[myid];

    if (recv[myid]>=0) begin++;
    if (send[myid]>=0) end--;

    last = ncount;
    recompute = false;

				// Report particle allocation calculation
    if (verbose) {

      if (myid==0) {

	int begin1 = begin;
	int end1   = end;

	cout << "====================================================" << endl
	     << "Component: " << c0->name << endl
	     << "Node list for mirroring" << endl 
	     << "-----------------------" << endl
	     << setw(6) << "Node" 
	     << setw(6) << "Count"
	     << setw(6) << "Send" 
	     << setw(6) << "Recv" 
	     << setw(6) << "Begin" 
	     << setw(6) << "End" 
	     << endl;
	
	for (int n=0; n<numprocs; n++) {

	  if (n) {
	    MPI_Recv(&begin1, 1, MPI_INT, n, 131, MPI_COMM_WORLD, &status);
	    MPI_Recv(&end1,   1, MPI_INT, n, 132, MPI_COMM_WORLD, &status);
	  }

	  cout << setw(6) << n << setw(6) << ncount[n];
	  if (send[n]<0) cout << setw(6) << "*";
	  else cout << setw(6) << send[n];
	  if (recv[n]<0) cout << setw(6) << "*";
	  else cout << setw(6) << recv[n];
	  cout << setw(6) << begin1 << setw(6) << end1 << endl;
	}

	cout << "====================================================" << endl;
	
      } else {
	MPI_Send(&begin, 1, MPI_INT, 0, 131, MPI_COMM_WORLD);
	MPI_Send(&end,   1, MPI_INT, 0, 132, MPI_COMM_WORLD);
      }
				// Should always have an even number of 
				// particles on each node after end points 
				// are removed
      int total = end - begin;
      if (total>0) assert( 2*(total/2) == total );
    }
    
  }

}

void SatFix::check_send()
{
  if (verbose) {
    bool ferror = false;
    for (int k=0; k<3; k++) {
      if (isnan(c0->Part(end)->pos[k])) ferror = true;
      if (isnan(c0->Part(end)->vel[k])) ferror = true;
      if (isnan(c0->Part(end)->acc[k])) ferror = true;
    }
    if (ferror) {
      cout << "Process " << myid << ": error in coordindates to be sent!" << endl;
    }
  }
  
}

void SatFix::check_body(int n)
{
  if (verbose) {
    bool ferror = false;
    for (int k=0; k<3; k++) {
      if (isnan(c0->Part(n)->pos[k])) ferror = true;
      if (isnan(c0->Part(n)->vel[k])) ferror = true;
      if (isnan(c0->Part(n)->acc[k])) ferror = true;
    }
    if (ferror) {
      cout << "Process " << myid << ": error in coordindates, n=" << n << "!" << endl;
    }
  }
}

void SatFix::check_recv()
{
  if (verbose) {
    bool ferror = false;
    for (int k=0; k<3; k++) {
      if (isnan(c0->Part(0)->pos[k])) ferror = true;
      if (isnan(c0->Part(0)->vel[k])) ferror = true;
      if (isnan(c0->Part(0)->acc[k])) ferror = true;
    }
    if (ferror) {
      cout << "Process " << myid << ": error in receiving coordindates!" << endl;
    }
  }
}


void SatFix::print_recv()
{
  if (verbose) {
    cout << endl << "Process " << myid << ": received pos=";
    for (int k=0; k<3; k++) cout << setw(16) << c0->Part(0)->pos[k];
    cout << endl << "Process " << myid << ": received vel=";
    for (int k=0; k<3; k++) cout << setw(16) << c0->Part(0)->vel[k];
    cout << endl << "Process " << myid << ": received acc=";
    for (int k=0; k<3; k++) cout << setw(16) << c0->Part(0)->acc[k];
  }
}

void SatFix::print_send()
{
  if (verbose) {
    cout << endl << "Process " << myid << ": sent pos=";
    for (int k=0; k<3; k++) cout << setw(16) << c0->Part(end)->pos[k];
    cout << endl << "Process " << myid << ": sent vel=";
    for (int k=0; k<3; k++) cout << setw(16) << c0->Part(end)->vel[k];
    cout << endl << "Process " << myid << ": sent acc=";
    for (int k=0; k<3; k++) cout << setw(16) << c0->Part(end)->acc[k];
  }
}

