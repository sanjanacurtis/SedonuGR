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

#include "global_options.h"
#include "Transport.h"
#include "Species.h"
#include "Grid.h"
#include <cstring>
#include "EinsteinHelper.h"

using namespace std;
namespace pc = physical_constants;

void Transport::propagate_particles()
{
	unsigned nu_index[1];

	if(verbose && rank0) cout << "# Propagating particles..." << endl;

	//--- MOVE THE PARTICLES AROUND ---
	// particle list is changing size due to splitting, so must go through repeatedly
	unsigned start=0, end=0;
	do{
	  start = end;
	  end = particles.size();

		#pragma omp parallel for schedule(dynamic)
		for(unsigned i=start; i<end; i++){
			Particle* p = &particles[i];
			#pragma omp atomic
			n_active[p->s]++;
			if(p->fate == moving) propagate(p);
			if(p->fate == escaped){
				const double nu = p->kup[3]/(2.0*pc::pi) * pc::c; // assumes metric is essentially Minkowski
				double D[3] = {p->kup[0], p->kup[1], p->kup[2]};
				Metric::normalize_Minkowski<3>(D);
				#pragma omp atomic
				n_escape[p->s]++;
				#pragma omp atomic
				L_net_esc[p->s] += p->N * nu*pc::h;
				#pragma omp atomic
				N_net_esc[p->s] += p->N;
				nu_index[0] = grid->nu_grid_axis.bin(nu);
				grid->spectrum[p->s].count(D, nu_index, nu, p->N * nu*pc::h);
			}
			PRINT_ASSERT(p->fate, !=, moving);
		} //#pragma omp parallel for
	} while(particles.size()>end);

	double tot=0,core=0,rouletted=0,esc=0;
	#pragma omp parallel for reduction(+:tot,core,rouletted,esc)
	for(unsigned i=0; i<particles.size(); i++){
		if(particles[i].fate == moving){
			if(rank0) cout << particles[i].fate << endl;
			if(rank0) cout << i << endl;
			PRINT_ASSERT(particles[i].fate,!=,moving);
		}
		const double nu = particles[i].kup[3]/(2.0*pc::pi) * pc::c;
		const double e  = particles[i].N * nu*pc::h;
		if(particles[i].fate!=rouletted) tot       += e;
		if(particles[i].fate==escaped  ) esc       += e;
		if(particles[i].fate==absorbed ) core      += e;
		if(particles[i].fate==rouletted) rouletted += e;
	}

	particle_total_energy += tot;
	particle_core_abs_energy += core;
	particle_rouletted_energy += rouletted;
	particle_escape_energy += esc;

	// remove the dead particles
	particles.resize(0);
}

//--------------------------------------------------------
// Decide what happens to the particle
//--------------------------------------------------------
void Transport::which_event(EinsteinHelper *eh, ParticleEvent *event) const{
	PRINT_ASSERT(eh->p.N, >, 0);
	PRINT_ASSERT(eh->z_ind,>=,0);

	double d_zone     = numeric_limits<double>::infinity();
	double d_interact = numeric_limits<double>::infinity();

	// FIND D_ZONE= ====================================================================
	double d_zone_min = step_size * grid->zone_min_length(eh->z_ind);
	double d_zone_boundary = grid->zone_cell_dist(eh->p.xup, eh->z_ind) + TINY*d_zone_min;
	d_zone = max(d_zone_min, d_zone_boundary);
	d_zone *= eh->g.dot<4>(eh->u,eh->p.kup) / eh->g.ndot(eh->p.kup); // convert to comoving frame
	PRINT_ASSERT(d_zone, >, 0);

	// FIND D_INTERACT =================================================================
	double relevant_opacity = eh->scatopac;
	if(exponential_decay) relevant_opacity += eh->absopac;
	if (relevant_opacity == 0) d_interact = numeric_limits<double>::infinity();
	else                       d_interact = static_cast<double>(eh->p.tau / relevant_opacity);
	PRINT_ASSERT(d_interact,>=,0);

	// find out what event happens (shortest distance) =====================================
	*event  = nothing;
	double d_smallest = d_zone;
	if( d_interact <= d_smallest ){
		*event = interact;
		d_smallest = d_interact;
	}
	eh->ds_com = d_smallest;
	PRINT_ASSERT(eh->ds_com, >=, 0);
	PRINT_ASSERT(eh->ds_com, <, INFINITY);
}


void Transport::boundary_conditions(EinsteinHelper *eh) const{
	PRINT_ASSERT(eh->p.fate,==,moving);

	if(r_core>0 && grid->radius(eh->p.xup)<r_core) eh->p.fate = absorbed;
	else if(eh->z_ind<0){
		grid->symmetry_boundaries(eh, step_size);
		update_eh(eh);
	}
}

void Transport::tally_radiation(const EinsteinHelper *eh, const int this_exp_decay) const{
	PRINT_ASSERT(eh->z_ind, >=, 0);
	PRINT_ASSERT(eh->z_ind, <, grid->rho.size());
	PRINT_ASSERT(eh->ds_com, >=, 0);
	PRINT_ASSERT(eh->p.N, >, 0);
	PRINT_ASSERT(eh->nu(), >, 0);
	double to_add = 0;
	double decay_factor = 1.0 - exp(-eh->absopac * eh->ds_com); //same in both frames

	// tally in contribution to zone's distribution function (lab frame)
	if(this_exp_decay && eh->absopac>0) to_add = eh->p.N / eh->absopac * decay_factor;
	else to_add = eh->p.N * eh->ds_com;
	to_add *= eh->nu()*pc::h;
	PRINT_ASSERT(to_add,<,INFINITY);

	double kup_tet[4];
	eh->coord_to_tetrad(eh->p.kup, kup_tet);
	grid->distribution[eh->p.s]->rotate_and_count(kup_tet, eh->p.xup, eh->dir_ind, eh->nu(), to_add);

	// store absorbed energy in *comoving* frame (will turn into rate by dividing by dt later)
	if(this_exp_decay) to_add = eh->p.N * decay_factor;
	else to_add = eh->p.N * eh->ds_com * eh->absopac;
	to_add *=  eh->nu()*pc::h;
	PRINT_ASSERT(to_add,>=,0);

	Tuple<double,4> tmp_fourforce;
	for(unsigned i=0; i<4; i++){
		#pragma omp atomic
		grid->fourforce_abs[eh->z_ind][i] += kup_tet[i] * pc::h*pc::c/(2.*pc::pi) * to_add;
	}

	// store absorbed lepton number (same in both frames, except for the
	// factor of this_d which is divided out later
	if(species_list[eh->p.s]->lepton_number != 0){
		to_add /= (eh->nu()*pc::h);
		to_add *= species_list[eh->p.s]->lepton_number;
		#pragma omp atomic
		grid->l_abs[eh->z_ind] += to_add;
	}
}

void Transport::move(EinsteinHelper *eh){
	PRINT_ASSERT(eh->p.tau,>=,0);
	PRINT_ASSERT(eh->ds_com,>=,0);

	// translate the particle
	grid->integrate_geodesic(eh);

	// reduce the particle's remaining optical depth
	double relevant_opac = eh->scatopac;
	if(exponential_decay) relevant_opac += eh->absopac;
	if(relevant_opac>0){
		double old_tau = eh->p.tau;
		double new_tau = eh->p.tau - relevant_opac * eh->ds_com;
		PRINT_ASSERT(new_tau,>=,-TINY*old_tau);
		eh->p.tau = max(0.0,new_tau);
	}

	// appropriately reduce the particle's energy
	if(exponential_decay){
		eh->p.N *= exp(-eh->absopac * eh->ds_com);
		window(eh);
	}

	update_eh(eh);
}


//--------------------------------------------------------
// Propagate a single monte carlo particle until
// it  escapes, is absorbed, or the time step ends
//--------------------------------------------------------
void Transport::propagate(Particle* p)
{
	double v[3];
	ParticleEvent event;

	EinsteinHelper eh;
	eh.p = *p;
	update_eh(&eh);
	
	PRINT_ASSERT(eh.p.fate, ==, moving);

	while (eh.p.fate == moving)
	{
		PRINT_ASSERT(eh.nu(), >, 0);

		// get all the opacities
		grid->get_opacity(&eh);

		// decide which event happens
		which_event(&eh,&event);

		// accumulate counts of radiation energy, absorption, etc
		if(eh.z_ind>=0) tally_radiation(&eh,exponential_decay);

		// move particle the distance
		move(&eh);
		if(eh.p.fate==moving) boundary_conditions(&eh);
		if(eh.p.fate==moving && event==interact) event_interact(&eh);
	}

	// copy particle back out of LorentzHelper
	*p = eh.p;
	PRINT_ASSERT(p->fate, !=, moving);
}
