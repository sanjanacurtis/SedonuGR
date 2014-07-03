#include "species_general.h"
#include "transport.h"
#include "locate_array.h"
#include <cassert>

//-----------------------------------------------------------------
// get opacity at the frequency
//-----------------------------------------------------------------
void species_general::get_opacity(const particle* p, const double dshift, double* opac, double* abs_frac) const
{
	assert(p->ind >= -1);

	if(p->ind == -1){ // particle is within inner boundary
		*opac = 0;
		*abs_frac = 0;
	}

	else{
		// comoving frame frequency
		double nu = p->nu*dshift;

		// absorption and scattering opacities
		double a = max(nu_grid.value_at(nu, abs_opac[p->ind]),0.0);
		double s = max(nu_grid.value_at(nu,scat_opac[p->ind]),0.0);

		// output - net opacity
		*opac = a+s;
		assert(*opac>=0);

		// output - absorption fraction
		*abs_frac = (a+s>0 ? a/(a+s) : 0);
		assert(0<=*abs_frac && *abs_frac<=1.0);
	}
}
