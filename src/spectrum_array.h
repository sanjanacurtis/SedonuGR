#ifndef _SPECTRUM_ARRAY_H
#define _SPECTRUM_ARRAY_H 1

#include <string>
#include "global_options.h"
#include "particle.h"
#include "locate_array.h"
#include "transport.h"

using namespace std;

// default values
#define DEFAULT_NAME "spectrum_array"

class spectrum_array {

private:

	// bin arrays
	// values represent bin upper walls (the single locate_array.min value is the leftmost wall)
	// underflow is combined into leftmost bin (right of the locate_array.min)
	// overflow is combined into the rightmost bin (left of locate_array[size-1])
	locate_array nu_grid;
	locate_array mu_grid;
	locate_array phi_grid;

	// counting arrays
	vector<double> flux;

	// Indexing
	int index(const int nu_bin,const int mu_bin,const int phi_bin) const;
	int  nu_bin(const int index) const;
	int  mu_bin(const int index) const;
	int phi_bin(const int index) const;

public:

	// Initialize
	void init(const locate_array nu_grid, const locate_array mu_grid, const locate_array phi_grid);
	void init(const std::vector<double> nu_grid, const int n_mu, const int n_phi);

	// MPI functions
	void MPI_average();

	// Count a packets
	void count(const vector<double>& D, const double nu, const double E);

	//  void normalize();
	void rescale(const double);
	void wipe();

	// integrate over nu,mu,phi
	double integrate() const;
	void integrate_over_direction(vector<double>& edens) const;

	// get bin centers and indices
	int size() const;
	double get(const int index) const;
	double  nu_center(const int index) const;
	double  mu_center(const int index) const;
	double phi_center(const int index) const;

	// Print out
	void print(const int iw, const int species) const;
};

#endif
