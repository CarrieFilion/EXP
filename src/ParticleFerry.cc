#include <iostream>
#include <iomanip>
#include <cstdlib>
#include "global.H"
#include "ParticleFerry.H"
#include "pHOT.H"

// #define DEBUG

void ParticleFerry::particleBufInit()
{
  bufsiz = 0;

  // double mass
  //
  bufsiz += sizeof(double);

  // double pos[3]
  //
  bufsiz += 3*sizeof(double);

  // double vel[3]
  //
  bufsiz += 3*sizeof(double);

  // double acc[3]
  //
  bufsiz += 3*sizeof(double);
  
  // double pot
  //
  bufsiz += sizeof(double);

  // double potext
  //
  bufsiz += sizeof(double);

  // std::vector<int> iattrib
  //
  bufsiz += sizeof(int)*nimax;

  // std::vector<double> dattrib
  //
  bufsiz += sizeof(double)*ndmax;

  // unsigned level
  //
  bufsiz += sizeof(unsigned);

  // float dtreq
  //
  bufsiz += sizeof(float);

  // float scale
  //
  bufsiz += sizeof(float);

  // float effort
  //
  bufsiz += sizeof(float);
  
  // position of indx
  //
  idxpos = bufsiz;

  // unsigned long indx
  //
  bufsiz += sizeof(unsigned long);

  // position of tree
  //
  treepos = bufsiz;

  // unsigned tree
  //
  bufsiz += sizeof(unsigned);
  
  // position of key
  //
  keypos = bufsiz;

#ifdef I128
  // uint128 key
  //
  bufsiz += sizeof(uint128);
#else
  // unsigned long key
  //
  bufsiz += sizeof(unsigned long);
#endif
}

void ParticleFerry::particlePack(const Particle& in, char* buffer)
{
  size_t pos = 0;

  // double mass
  //
  memcpy(&buffer[pos], &in.mass, sizeof(double));
  pos += sizeof(double);

  // double pos[3]
  //
  memcpy(&buffer[pos], &in.pos[0], 3*sizeof(double));
  pos += 3*sizeof(double);

  // double vel[3]
  //
  memcpy(&buffer[pos], &in.vel[0], 3*sizeof(double));
  pos += 3*sizeof(double);

  // double acc[3]
  //
  memcpy(&buffer[pos], &in.acc[0], 3*sizeof(double));
  pos += 3*sizeof(double);
  
  // double pot
  //
  memcpy(&buffer[pos], &in.pot, sizeof(double));
  pos += sizeof(double);

  // double potext
  //
  memcpy(&buffer[pos], &in.potext, sizeof(double));
  pos += sizeof(double);

  // std::vector<int> iattrib
  //
  memcpy(&buffer[pos], &in.iattrib[0], nimax*sizeof(int));
  pos += sizeof(int)*nimax;

  // std::vector<double> dattrib
  //
  memcpy(&buffer[pos], &in.dattrib[0], ndmax*sizeof(double));
  pos += sizeof(double)*ndmax;

  // unsigned level
  //
  memcpy(&buffer[pos], &in.level, sizeof(unsigned));
  pos += sizeof(unsigned);

  // float dtreq
  //
  memcpy(&buffer[pos], &in.dtreq, sizeof(float));
  pos += sizeof(float);

  // float scale
  //
  memcpy(&buffer[pos], &in.scale, sizeof(float));
  pos += sizeof(float);

  // float effort
  //
  memcpy(&buffer[pos], &in.effort, sizeof(float));
  pos += sizeof(float);
  
  // unsigned long indx
  //
  memcpy(&buffer[pos], &in.indx, sizeof(unsigned long));
  pos += sizeof(unsigned long);

  // unsigned tree
  //
  memcpy(&buffer[pos], &in.tree, sizeof(unsigned));
  pos += sizeof(unsigned);
  
#ifdef I128
  // uint128 key
  //
  memcpy(&buffer[pos], &in.key, sizeof(uint128));
  pos += sizeof(uint128);
#else
  // unsigned long key
  //
  memcpy(&buffer[pos], &in.key, sizeof(unsigned long));
  pos += sizeof(unsigned long);
#endif

  // Sanity check
  //
  if (in.tree > 0) {
    if ( (in.tree < pk_lo) || (in.tree >= pk_hi) ) {
      cout << "Error!! [5], id=" << myid 
	   << ": tree=" << in.tree
	   << " seq=" << in.indx
	   << " (x, y, z)={" << in.pos[0] << ", " << in.pos[1]
	   << ", " << in.pos[2]
	   << endl;
    }
  }
}

void ParticleFerry::particleUnpack(Particle& out, char* buffer)
{
  size_t pos = 0;

  // double mass
  //
  memcpy(&out.mass, &buffer[pos], sizeof(double));
  pos += sizeof(double);

  // double pos[3]
  //
  memcpy(&out.pos[0], &buffer[pos], 3*sizeof(double));
  pos += 3*sizeof(double);

  // double vel[3]
  //
  memcpy(&out.vel[0], &buffer[pos], 3*sizeof(double));
  pos += 3*sizeof(double);

  // double acc[3]
  //
  memcpy(&out.acc[0], &buffer[pos], 3*sizeof(double));
  pos += 3*sizeof(double);
  
  // double pot
  //
  memcpy(&out.pot, &buffer[pos], sizeof(double));
  pos += sizeof(double);

  // double potext
  //
  memcpy(&out.potext, &buffer[pos], sizeof(double));
  pos += sizeof(double);

  // std::vector<int> iattrib
  //
  out.iattrib.resize(nimax);
  memcpy(&out.iattrib[0], &buffer[pos], nimax*sizeof(int));
  pos += sizeof(int)*nimax;

  // std::vector<double> dattrib
  //
  out.dattrib.resize(ndmax);
  memcpy(&out.dattrib[0], &buffer[pos], ndmax*sizeof(double));
  pos += sizeof(double)*ndmax;

  // unsigned level
  //
  memcpy(&out.level, &buffer[pos], sizeof(unsigned));
  pos += sizeof(unsigned);

  // float dtreq
  //
  memcpy(&out.dtreq, &buffer[pos], sizeof(float));
  pos += sizeof(float);

  // float scale
  //
  memcpy(&out.scale, &buffer[pos], sizeof(float));
  pos += sizeof(float);

  // float effort
  //
  memcpy(&out.effort, &buffer[pos], sizeof(float));
  pos += sizeof(float);
  
  // unsigned long indx
  //
  memcpy(&out.indx, &buffer[pos], sizeof(unsigned long));
  pos += sizeof(unsigned long);

  // unsigned tree
  //
  memcpy(&out.tree, &buffer[pos], sizeof(unsigned));
  pos += sizeof(unsigned);
  
#ifdef I128
  // uint128 key
  //
  memcpy(&out.key, &buffer[pos], sizeof(uint128));
  pos += sizeof(uint128);
#else
  // unsigned long key
  //
  memcpy(&out.key, &buffer[pos], sizeof(unsigned long));
  pos += sizeof(unsigned long);
#endif

  // Sanity check
  //
  if (out.tree > 0) {
    if ( (out.tree < pk_lo) || (out.tree >= pk_hi) ) {
      cout << "Error!! [4], id=" << myid 
	   << ": tree=" << out.tree
	   << " seq=" << out.indx
	   << " (x, y, z)={" << out.pos[0] << ", " << out.pos[1]
	   << ", " << out.pos[2]
	   << endl;
    }
  }

}

ParticleFerry::ParticleFerry(int nimax, int ndmax) : nimax(nimax), ndmax(ndmax)
{
				// Determine size of buffer for a single particle
  particleBufInit();
				// Allocate internal buffer
  buf.resize(PFbufsz*bufsiz);

  bufpos    = 0;
  ibufcount = 0;

  pk_lo = 1u << (3*pkbits);
  pk_hi = 1u << (3*pkbits+1);

  key_lo = 1u;
  key_lo <<= (3*nbits);
  key_hi = 1u;
  key_hi <<= (3*nbits+1);

}

ParticleFerry::~ParticleFerry()
{
}

void ParticleFerry::ShipParticles(unsigned to, unsigned from, unsigned& total)
{
  MPI_Status status;

  _to    = to;
  _from  = from;
  _total = total;

  if (_from == myid) {
    MPI_Send(&_total, 1, MPI_UNSIGNED, _to, 29, MPI_COMM_WORLD);
    bufpos    = 0;
    ibufcount = 0;
    itotcount = 0;
  }
  
  if (_to == myid) {
    MPI_Recv(&_total, 1, MPI_UNSIGNED, _from, 29, MPI_COMM_WORLD, &status);
    bufpos    = 0;
    ibufcount = 0;
    itotcount = 0;
    total = _total;
  }
}

#ifdef I128
void ParticleFerry::SendParticle(Particle& ptc, unsigned seq, uint128 key)
#else
void ParticleFerry::SendParticle(Particle& ptc, unsigned seq, unsigned long key)
#endif
{
  // Add particle to buffer
  //
  particlePack(ptc, &buf[bufpos]);
  memcpy(&buf[bufpos+idxpos], &seq, sizeof(unsigned));
#ifdef I128
  memcpy(&buf[bufpos+keypos], &key, sizeof(uint128));
#else
  memcpy(&buf[bufpos+keypos], &key, sizeof(unsigned long));
#endif

  // If buffer is full, send the buffer and reset
  //
  bufpos += bufsiz;
  ibufcount++;
  itotcount++;
  if (ibufcount == PFbufsz || itotcount == _total) BufferSend();
}

void ParticleFerry::SendParticle(Particle& part)
{
  // Add particle to buffer
  //
  particlePack(part, &buf[bufpos]);
  bufpos += bufsiz;
  ibufcount++;
  itotcount++;

  // If buffer is full, send the buffer and reset
  //
  if (ibufcount == PFbufsz || itotcount == _total) BufferSend();
}

#ifdef I128
bool ParticleFerry::RecvParticle(Particle& ptc, unsigned& seq, uint128& key)
#else
bool ParticleFerry::RecvParticle(Particle& ptc, unsigned& seq, unsigned long& key)
#endif
{
  if (itotcount++ == _total) return false;

  if (ibufcount==0) BufferRecv();

  particleUnpack(ptc, &buf[bufpos]);
  seq = ptc.indx;
  key = ptc.key;

  ibufcount--;
  bufpos -= bufsiz;

  return true;
}

bool ParticleFerry::RecvParticle(Particle& part)
{
  if (itotcount++ == _total) return false;
  if (ibufcount==0) BufferRecv();

  bufpos -= bufsiz;
  ibufcount--;

  particleUnpack(part, &buf[bufpos]);
  if (part.indx==0 || part.mass<=0.0 || std::isnan(part.mass)) {
	cout << "BAD MASS!" << endl;
  }
#ifdef DEBUG
  if (part.indx==0) {
    cout << "ParticleFerry: process " << myid << " error in sequence" << endl;
  }
#endif
  return true;
}

void ParticleFerry::BufferSend()
{
  int totchar = ibufcount*bufsiz;

  MPI_Send(&ibufcount, 1,       MPI_INT,  _to, 2, MPI_COMM_WORLD);
  MPI_Send(&buf[0],    totchar, MPI_CHAR, _to, 3, MPI_COMM_WORLD);
#ifdef DEBUG
  cout << "ParticleFerry: process " << myid  << " send, tot=" << itotcount << endl;
  bufferKeyCheck();
#endif
  ibufcount = 0;
}

void ParticleFerry::BufferRecv()
{
  MPI_Status s;

  MPI_Recv(&ibufcount, 1,      MPI_INT,  _from, 2, MPI_COMM_WORLD, &s);

  bufpos = ibufcount*bufsiz;

  MPI_Recv(&buf[0],    bufpos, MPI_CHAR, _from, 3, MPI_COMM_WORLD, &s);
#ifdef DEBUG
  cout << "ParticleFerry: process " << myid  << " recv, tot=" << itotcount-1+ibufcount << endl;
  bufferKeyCheck();
#endif
}


void ParticleFerry::bufferKeyCheck()
{
				// Sanity check for pHOT keys
#ifdef I128
  uint128 minkey = 1u;
  minkey <<= 128 - 1;
  uint128 maxkey = 0u;
  unsigned err0 = 0;
  for (unsigned n=0; n<ibufcount; n++) {
    uint128 key; memcpy(&key, &buf[n*bufsiz+keypos], sizeof(uint128));
    minkey = min<uint128>(minkey, key);
    maxkey = max<uint128>(maxkey, key);
    if ((key < key_lo || key >= key_hi) && key > 0u) err0++;
  }
#else
  unsigned long minkey = 1u;
  minkey <<= sizeof(unsigned long)*8 - 1;
  unsigned long maxkey = 0u;
  unsigned err0 = 0;
  for (unsigned n=0; n<ibufcount; n++) {
    unsigned long key; memcpy(&key, &buf[n*bufsiz+keypos], sizeof(unsigned long));
    minkey = min<unsigned long>(minkey, key);
    maxkey = max<unsigned long>(maxkey, key);
    if ((key < key_lo || key >= key_hi) && key > 0u) err0++;
  }
#endif

  unsigned maxpexp = 1u;
  maxpexp <<= (3*pkbits);
  unsigned minpkey = 1u;
  minpkey <<= (32 - 1);
  unsigned maxpkey = 0u;
  unsigned err1 = 0;
  for (unsigned n=0; n<ibufcount; n++) {
    unsigned tree; memcpy(&tree, &buf[n*bufsiz+treepos], sizeof(unsigned));
    minpkey = min<unsigned>(minpkey, tree);
    maxpkey = max<unsigned>(maxpkey, tree);
    if ((tree < pk_lo || tree >= pk_hi) && tree > 0u) err1++;
  }

  unsigned wid = 3*nbits/4 + 3;

  if (err0)
    cerr << "ParticleFerry: Key err=" << err0 << endl << hex
	 << "ParticleFerry: min key=" << right << setw(wid) << minkey << endl 
	 << "ParticleFerry: max key=" << right << setw(wid) << maxkey << endl;
  if (err1)
    cout << "ParticleFerry: Cel err=" << dec << err1 << endl
	 << "ParticleFerry: min cel=" << right << setw(12) << minpkey << endl 
	 << "ParticleFerry: max cel=" << right << setw(12) << maxpkey << endl;

  return;
}
