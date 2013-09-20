#include <omp.h>
#include <mpi.h>
#include <stdio.h>
#include <iostream>
#include <math.h>
#include <string.h>
#include <vector>
#include "spectrum_array.h"
#include "physical_constants.h"

namespace pc = physical_constants;

//--------------------------------------------------------------
// Constructors
//--------------------------------------------------------------
spectrum_array::spectrum_array()
{
  strcpy(name,DEFAULT_NAME);
  a1=0; a2=0; a3=0;
  n_elements=0;
}

void spectrum_array::set_name(const char *n)
{
  strcpy(name,n);
}


//--------------------------------------------------------------
// Initialization and Allocation
//--------------------------------------------------------------
void spectrum_array::init(std::vector<double> t, std::vector<double> w,
			  int n_mu, int n_phi)
{
  // assign time grid
  double t_start = t[0];
  double t_stop  = t[1];
  double t_del   = t[2];
  time_grid.init(t_start,t_stop,t_del);
  int n_times  = time_grid.size();

  // assign wave grid
  double w_start = w[0];
  double w_stop  = w[1];
  double w_del   = w[2];
  wave_grid.init(w_start,w_stop,w_del);
  int n_wave   = wave_grid.size();

  // asign mu grid
  mu_grid.init(-1,1,n_mu);

  // asign phi grid
  phi_grid.init(0,2*pc::pi,n_phi);

  // index parameters
  n_elements  = n_times*n_wave*n_mu*n_phi;
  a3 = n_phi;
  a2 = n_mu*a3;
  a1 = n_wave*a2;

  // allocate memory
  click.resize(n_elements);
  flux.resize(n_elements);

  // clear 
  wipe();
}


//--------------------------------------------------------------
// Initialization and Allocation
//--------------------------------------------------------------
void spectrum_array::init(std::vector<double> tg, std::vector<double> wg, 
			  std::vector<double> mg, std::vector<double> pg)
{
  // initialize locate arrays
  time_grid.init(tg);
  wave_grid.init(wg);
  mu_grid.init(mg);
  phi_grid.init(pg);

  int n_times  = time_grid.size();
  int n_wave   = wave_grid.size();
  int n_mu     = mu_grid.size();
  int n_phi    = phi_grid.size();

  // index parameters
  n_elements  = n_times*n_wave*n_mu*n_phi;
  a3 = n_phi;
  a2 = n_mu*a3;
  a1 = n_wave*a2;

  // allocate memory
  click.resize(n_elements);
  flux.resize(n_elements);

  // clear 
  wipe();
}


//--------------------------------------------------------------
// Functional procedure: Wipe
//--------------------------------------------------------------
void spectrum_array::wipe()
{
  #pragma omp parallel for
  for (int i=0;i<click.size();i++) 
  {
    flux[i]   = 0;
    click[i]  = 0;
  }
}



//--------------------------------------------------------------
// handles the indexing: should be called in this order
//    time, wavelength, mu, phi
//--------------------------------------------------------------
int spectrum_array::index(int t, int l, int m, int p)
{
  return t*a1 + l*a2 + m*a3 + p;
}


//--------------------------------------------------------------
// count a particle
////--------------------------------------------------------------
void spectrum_array::count(double t, double w, double E, double *D)
{
  double mu  = D[2];
  double phi = atan2(D[1],D[0]);

  // locate bin number in all dimensions.
  // Set to zero if there is only one bin.
  int t_bin = (time_grid.size()==1 ? 0 : time_grid.locate(t)  );
  int l_bin = (wave_grid.size()==1 ? 0 : wave_grid.locate(w)  );
  int m_bin = (  mu_grid.size()==1 ? 0 :   mu_grid.locate(mu) );
  int p_bin = ( phi_grid.size()==1 ? 0 :  phi_grid.locate(phi));

  // keep all photons, even if off wavelength grid
  // locate does this automatically for the upper bound.
  if (l_bin <  0) l_bin = 0;

  // if off the grids, just return without counting
  // remember the bin index enumerates the left wall of the bin
  if ((t_bin < 0)||(m_bin < 0)||(p_bin < 0)) return;
  if ((time_grid.size() > 1) && (t_bin >= time_grid.size()-1)) return;
  if ((  mu_grid.size() > 1) && (m_bin >=   mu_grid.size()-1)) return;
  if (( phi_grid.size() > 1) && (p_bin >=  phi_grid.size()-1)) return;

  // add to counters
  int ind = index(t_bin,l_bin,m_bin,p_bin);

  #pragma omp atomic
  flux[ind]  += E;
  #pragma omp atomic
  click[ind] += 1;
}



//--------------------------------------------------------------
// print out
//--------------------------------------------------------------
void spectrum_array::print()
{
  FILE *out = fopen(name,"w");

  int n_times  = time_grid.size();
  int n_wave   = wave_grid.size();
  int n_mu     = mu_grid.size();
  int n_phi    = phi_grid.size();

  fprintf(out,"# %d %d %d %d\n",n_times,n_wave,n_mu,n_phi);

  for (int k=0;k<n_mu;k++)
    for (int m=0;m<n_phi;m++)
      for (int i=0;i<n_times;i++)
	for (int j=0;j<n_wave;j++) 
        {
	  int id = index(i,j,k,m);
	  if (n_times > 1)  fprintf(out,"%12.4e ",time_grid.center(i));;
	  if (n_wave > 1)   fprintf(out,"%12.4e ",wave_grid.center(j));
	  if (n_mu > 1)     fprintf(out,"%12.4f ",mu_grid.center(k));
	  if (n_phi> 1)     fprintf(out,"%12.4f ",phi_grid.center(m));

	  double norm = n_mu*n_phi*wave_grid.delta(j)*time_grid.delta(i);
	  fprintf(out,"%12.5e %10d\n", flux[id]/norm,click[id]);
	}
  fclose(out);
}


void  spectrum_array::rescale(double r)
{
  #pragma omp parallel for
  for (int i=0;i<flux.size();i++) flux[i] *= r;
}


//--------------------------------------------------------------
// MPI average the spectrum contents
//--------------------------------------------------------------
void spectrum_array::MPI_average()
{
  {
    vector<double> receive(n_elements,0);
    MPI_Allreduce(&flux.front(),&receive.front(),n_elements,MPI_DOUBLE,MPI_SUM,MPI_COMM_WORLD);
    flux.swap(receive);
  }

  {
    vector<int> receive(n_elements,0);
    MPI_Allreduce(&click.front(),&receive.front(),n_elements,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
    click.swap(receive);
  }

  int mpi_procs;
  MPI_Comm_size( MPI_COMM_WORLD, &mpi_procs );
  #pragma omp parallel for
  for (int i=0;i<n_elements;i++) 
  {
    flux[i]  /= mpi_procs;
    click[i] /= mpi_procs; 
  }
}
