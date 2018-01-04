/*
//  Copyright (c) 2015, California Institute of Technology and the Regents
//  of the University of California, based on research sponsored by the
//  United States Department of Energy. All rights reserved.
//
//  This file is part of Sedonu.
//
//  Sedonu is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Neither the name of the California Institute of Technology (Caltech)
//  nor the University of California nor the names of its contributors 
//  may be used to endorse or promote products derived from this software
//  without specific prior written permission.
//
//  Sedonu is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Sedonu.  If not, see <http://www.gnu.org/licenses/>.
//
*/

#include <omp.h>
#include "Transport.h"
#include "Species.h"
#include "Grid.h"
#include "global_options.h"

using namespace std;
namespace pc = physical_constants;

//------------------------------------------------------------
// emit new particles
//------------------------------------------------------------
void Transport::emit_particles()
{
	// complain if we're out of room for particles
	assert(n_emit_core_per_bin>0 || n_emit_zones_per_bin>=0);
	int n_emit = (n_emit_core_per_bin + n_emit_zones_per_bin*grid->rho.size()) * species_list.size()*grid->nu_grid_axis.size();
	if (total_particles() + n_emit > max_particles){
		if(rank0){
			cout << "Total particles: " << total_particles() << endl;
			cout << "n_emit: " << n_emit << endl;
			cout << "max_particles: " << max_particles << endl;
			cout << "# ERROR: Not enough particle space\n";
		}
		exit(10);
	}


	// emit from the core and/or the zones
	if(verbose && rank0) cout << "# Emitting particles..." << endl;
	if(n_emit_core_per_bin>0)  emit_inner_source_by_bin();
	if(n_emit_zones_per_bin>0) emit_zones_by_bin();
}

//------------------------------------------------------------
// inject particles from a central luminous source
// Currently written to emit photons with 
// blackblody spectrum based on T_core and L_core
//------------------------------------------------------------
void Transport::emit_inner_source_by_bin(){
	int size_before = particles.size();
	int n_attempted = 0;
	double weight = 1./((double)n_emit_core_per_bin);

	#pragma omp parallel for reduction(+:n_attempted)
	for(unsigned s=0; s<species_list.size(); s++){
		n_attempted += n_emit_core_per_bin * grid->nu_grid_axis.size();
		for(unsigned g=0; g<grid->nu_grid_axis.size(); g++){
			for(int k=0; k<n_emit_core_per_bin; k++) create_surface_particle(weight,s,g);
		}
	}

	int n_created = particles.size()-size_before;
	if(verbose && rank0) cout << "#   emit_inner_source_by_bin() created = " << n_created << " particles on rank 0 ("
			<< n_attempted-n_created << " rouletted during emission)" << endl;
}


//--------------------------------------------------------------------------
// emit particles due to viscous heating
//--------------------------------------------------------------------------
void Transport::emit_zones_by_bin(){
	int size_before = particles.size();
	int n_attempted = 0;
	double weight = 1./((double)n_emit_zones_per_bin);

	#pragma omp parallel for reduction(+:n_attempted)
	for (unsigned z_ind=MPI_myID; z_ind<grid->rho.size(); z_ind+=MPI_nprocs) if(grid->zone_radius(z_ind) >= r_core){

		for(unsigned s=0; s<species_list.size(); s++){
			n_attempted += n_emit_zones_per_bin * grid->nu_grid_axis.size();
			for(unsigned g=0; g<grid->nu_grid_axis.size(); g++){
				for(int k=0; k<n_emit_zones_per_bin; k++)
					create_thermal_particle(z_ind,weight,s,g);
			}
		}

		// record emissivity
	}

	int n_created = particles.size() - size_before;
	if(verbose && rank0) cout << "#   emit_zones_by_bin() created " << n_created << " particles on rank 0 ("
			<< n_attempted-n_created << " rouletted immediately)" << endl;
}


//------------------------------------------------------------
// General function to create a particle in zone i
// emitted isotropically in the comoving frame. 
// Useful for thermal radiation emitted all througout
// the grid
//------------------------------------------------------------
void Transport::create_thermal_particle(const int z_ind,const double weight, const unsigned int s, const unsigned int g)
{
	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)grid->rho.size());
	unsigned dir_ind[NDIMS];
	grid->rho.indices(z_ind,dir_ind);

	Particle p;
	p.fate = moving;

	// random sample position in zone
	grid->sample_in_zone(z_ind,&rangen,p.xup);
	p.xup[3] = 0;

	// get the velocity
	double v[3];
	grid->interpolate_fluid_velocity(p.xup,v,z_ind);

	// species
	p.s = s;
	PRINT_ASSERT(p.s,>=,0);
	PRINT_ASSERT(p.s,<,(int)species_list.size());

	// frequency
	double nu_min = grid->nu_grid_axis.bottom(g);
	double nu_max = grid->nu_grid_axis.top[g];
	double nu = rangen.uniform(nu_min, nu_max);
	p.N = grid->BB[s].interpolate(p.xup,dir_ind) * grid->abs_opac[s].interpolate(p.xup,dir_ind);
	p.N *= weight /(pc::c*pc::c) * 4.*pc::pi * grid->nu_grid_axis.delta3(g)/3.0 * grid->zone_com_3volume(z_ind);


	// set up LorentzHelper
	EinsteinHelper eh;
	eh.p = p;
	update_eh(&eh);

	// emit isotropically in comoving frame
	double kup_tet[4];
	grid->isotropic_kup_tet(nu,kup_tet,p.xup,&rangen);
	eh.tetrad_to_coord(kup_tet,eh.p.kup);

	// sample tau
	grid->get_opacity(&eh);
	eh.p.tau = sample_tau(&rangen);
	window(&eh);

	// add to particle vector
	if(eh.p.fate == moving){
		PRINT_ASSERT(particles.size(),<,particles.capacity());
		PRINT_ASSERT(eh.p.N,>,0);
		PRINT_ASSERT(eh.p.tau,>,0);
		#pragma omp critical
		particles.push_back(eh.p);

		// count up the emitted energy in each zone
		#pragma omp atomic
		N_net_lab[eh.p.s] += eh.p.N;
		#pragma omp atomic
		grid->l_emit[z_ind] += eh.p.N * species_list[eh.p.s]->lepton_number * pc::c;
		for(unsigned i=0; i<4; i++){
			#pragma omp atomic
			grid->fourforce_emit[z_ind][i] -= kup_tet[i] * pc::c*pc::h/(2.*pc::pi) * kup_tet[i];
		}
	}
}


//------------------------------------------------------------
// General function to create a particle on the surface
// emitted isotropically outward in the comoving frame. 
//------------------------------------------------------------
void Transport::create_surface_particle(const double weight, const unsigned int s, const unsigned int g)
{
	PRINT_ASSERT(weight,>,0);
	PRINT_ASSERT(weight,!=,INFINITY);

	Particle plab;
	plab.fate = moving;

	// pick initial position on photosphere
	double D[3];
	grid->random_core_x_D(r_core,&rangen,plab.xup,D);
	plab.xup[3] = 0;

	// get index of current zone
	const int z_ind = grid->zone_index(plab.xup);
	PRINT_ASSERT(z_ind,>=,0);

	// sample the species
	plab.s = s;
	PRINT_ASSERT(plab.s,>=,0);
	PRINT_ASSERT(plab.s,<,(int)species_list.size());

	// sample the frequency
	double nu3_min = pow(grid->nu_grid_axis.bottom(g), 3);
	double nu3_max = pow(grid->nu_grid_axis.top[g],    3);
	double nu3 = rangen.uniform(nu3_min, nu3_max);
	double nu = pow(nu3, 1./3.);
	plab.kup[3] = nu / pc::c * 2.0*pc::pi;
	for(int i=0; i<3; i++) plab.kup[i] = D[i] * plab.kup[3];

	//get the number of neutrinos in the particle
	double T = species_list[s]->T_core;
	double mu = species_list[s]->mu_core;
	double multiplier = species_list[s]->core_lum_multiplier * species_list[s]->weight;
	PRINT_ASSERT(nu,>,0);
	plab.N = number_blackbody(T,mu,nu)   // #/s/cm^2/sr/(Hz^3/3)
			* 1                          //   s
			* (4.0*pc::pi*r_core*r_core) //     cm^2
			* pc::pi                     //          sr
			* grid->nu_grid_axis.delta3(g)/3.0 //        Hz^3/3
			* multiplier                 // overall scaling
			* weight;                    // 1/number of samples

	// set up LorentzHelper
	EinsteinHelper eh;
	eh.p = plab;
	update_eh(&eh);

	// sample the optical depth
	grid->get_opacity(&eh);
	eh.p.tau = sample_tau(&rangen);
	window(&eh);

	// add to particle vector
	if(eh.p.fate == moving){
		PRINT_ASSERT(particles.size(),<,particles.capacity());
	    #pragma omp critical
		particles.push_back(eh.p);
	    #pragma omp atomic
		N_core_lab[eh.p.s] += eh.p.N;
	}
}
