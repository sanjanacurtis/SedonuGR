#include <limits>
#include <omp.h>
#include <mpi.h>
#include <iostream>
#include <math.h>
#include <string.h>
#include <vector>
#include <fstream>
#include "global_options.h"
#include "spectrum_array.h"

//--------------------------------------------------------------
// Initialization and Allocation
//--------------------------------------------------------------
void spectrum_array::init(const std::vector<double> w,
		const int n_mu, const int n_phi)
{
	// assign wave grid
	double w_start = w[0];
	double w_stop  = w[1];
	double w_del   = w[2];
	wave_grid.init(w_start,w_stop,w_del);
	int n_wave   = wave_grid.size();

	// asign mu grid
	mu_grid.init(-1,1,n_mu);

	// asign phi grid
	phi_grid.init(-pc::pi,pc::pi,n_phi);

	// index parameters
	unsigned n_elements  = n_wave*n_mu*n_phi;

	// allocate memory
	flux.resize(n_elements);

	// clear
	wipe();
}


//--------------------------------------------------------------
// Initialization and Allocation
//--------------------------------------------------------------
void spectrum_array::init(const locate_array wg,
		const locate_array mg, const locate_array pg)
{
	// initialize locate arrays by swapping with the inputs
	wave_grid.swap(wg);
	mu_grid.swap(mg);
	phi_grid.swap(pg);

	int n_wave   = wave_grid.size();
	int n_mu     = mu_grid.size();
	int n_phi    = phi_grid.size();

	// index parameters
	unsigned n_elements  = n_wave*n_mu*n_phi;

	// allocate memory
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
	for(unsigned i=0;i<flux.size();i++) flux[i]   = 0;
}



//--------------------------------------------------------------
// handles the indexing: should be called in this order
//    group, mu, phi
//--------------------------------------------------------------
int spectrum_array::index(const int g, const int m, const int p) const
{
	assert(g>=0);
	assert(m>=0);
	assert(p>=0);
	int n_phi = phi_grid.size();
	int n_wave = wave_grid.size();
	int n_mu = mu_grid.size();
	int a3 = n_phi;
	int a2 = n_mu*a3;
	const int ind = g*a2 + m*a3 + p;
	assert(ind >= 0);
	assert(ind < n_phi*n_wave*n_mu);
	return ind;
}


//--------------------------------------------------------------
// count a particle
////--------------------------------------------------------------
void spectrum_array::count(const double w, const double E, const vector<double> D)
{
	assert(D.size()==3);
	double mu  = D[2];
	double phi = atan2(D[1],D[0]);

	// if off the LEFT of mu/phi grids, just return without counting
	if ((mu<mu_grid.min) || (phi<phi_grid.min)) return;

	// locate bin number in all dimensions.
	unsigned l_bin = wave_grid.locate(w);
	unsigned m_bin =   mu_grid.locate(mu);
	unsigned p_bin =  phi_grid.locate(phi);

	// if off the RIGHT of time/mu/phi grids, just return without counting
	if((m_bin ==   mu_grid.size()) || (p_bin ==  phi_grid.size())) return;

	// if off RIGHT of wavelength grid, store in last bin (LEFT is accounted for by locate)
	if (l_bin == wave_grid.size()) l_bin--;

	// add to counters
	int ind = index(l_bin,m_bin,p_bin);

        #pragma omp atomic
	flux[ind]  += E;
}



//--------------------------------------------------------------
// print out
//--------------------------------------------------------------
void spectrum_array::print(const int iw, const int species) const
{
	ofstream outf;
	string filename = "spectrum_species"+to_string(species);
	transport::open_file(filename.c_str(),iw,outf);

	unsigned n_wave   = wave_grid.size();
	unsigned n_mu     = mu_grid.size();
	unsigned n_phi    = phi_grid.size();

	outf << "# " << "n_wave:" << n_wave << " " << "n_mu:" << n_mu << " " << "n_phi:" << n_phi << endl;
	outf << "# ";
	outf << (n_wave >1 ? "frequency(Hz) " : "");
	outf << (n_mu   >1 ? "mu "            : "");
	outf << (n_phi  >1 ? "phi "           : "");
	outf << "integrated_flux(erg) counts" << endl;

	for (unsigned k=0;k<n_mu;k++)
		for (unsigned m=0;m<n_phi;m++)
			for (unsigned j=0;j<n_wave;j++)
			{
				int id = index(j,k,m);
				if (n_wave > 1)   outf << wave_grid.center(j) << " ";
				if (n_mu > 1)     outf << mu_grid.center(k)   << " ";
				if (n_phi> 1)     outf << phi_grid.center(m)  << " ";

				// the delta is infinity if the bin is a catch-all.
				double wdel = wave_grid.delta(j);
				double norm = n_mu*n_phi
				  * ( wdel < numeric_limits<double>::infinity() ? wdel : 1 );
				outf << flux[id]/norm << endl;
			}
	outf.close();
}


void  spectrum_array::rescale(double r)
{
        #pragma omp parallel for
	for(unsigned i=0;i<flux.size();i++) flux[i] *= r;
}


//--------------------------------------------------------------
// MPI average the spectrum contents
//--------------------------------------------------------------
// only process 0 gets the reduced spectrum to print
void spectrum_array::MPI_average()
{
	int receiving_ID = 0;
	const unsigned n_elements = wave_grid.size()*mu_grid.size()*phi_grid.size();

	// average the flux (receive goes out of scope after section)
	vector<double> receive;
	receive.resize(n_elements);
	MPI_Reduce(&flux.front(), &receive.front(), n_elements, MPI_DOUBLE, MPI_SUM, receiving_ID, MPI_COMM_WORLD);
	flux.swap(receive);

	// only have the receiving ID do the division
	int mpi_procs, myID;
	MPI_Comm_size( MPI_COMM_WORLD, &mpi_procs );
	MPI_Comm_rank( MPI_COMM_WORLD, &myID      );
	if(myID == receiving_ID){
                #pragma omp parallel for
		for(unsigned i=0;i<n_elements;i++) rescale(1./(double)mpi_procs);
	}
}
