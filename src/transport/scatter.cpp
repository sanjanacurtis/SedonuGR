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

#include "Species.h"
#include "Transport.h"
#include "Grid.h"
#include "global_options.h"

using namespace std;
namespace pc = physical_constants;

//------------------------------------------------------------
// physics of absorption/scattering
//------------------------------------------------------------
void Transport::event_interact(EinsteinHelper* eh){
	PRINT_ASSERT(eh->z_ind,>=,0);
	PRINT_ASSERT(eh->z_ind,<,(int)grid->rho.size());
	PRINT_ASSERT(eh->p.N,>,0);
	PRINT_ASSERT(eh->p.fate,==,moving);

	// absorb part of the packet
	if(!exponential_decay) eh->p.N *= 1.0 - eh->absopac/(eh->absopac + eh->scatopac);
	scatter(eh);

	// resample the path length
	if(eh->p.fate==moving) eh->p.tau = sample_tau(&rangen);

	// window the particle
	if(eh->p.fate==moving) window(eh);

	// sanity checks
	if(eh->p.fate==moving){
		PRINT_ASSERT(eh->nu(),>,0);
		PRINT_ASSERT(eh->nu(),>,0);
	}
}


// decide whether to kill a particle
void Transport::window(EinsteinHelper *eh){
	PRINT_ASSERT(eh->p.N,>=,0);
	PRINT_ASSERT(eh->p.fate,!=,rouletted);

	// Roulette if too low energy
	while(eh->p.N<=min_packet_number && eh->p.fate==moving){
		if(rangen.uniform() < 0.5) eh->p.fate = rouletted;
		else eh->p.N *= 2.0;
	}
	if(eh->p.fate==moving) PRINT_ASSERT(eh->p.N,>=,min_packet_number);

	// split if too high energy, if enough space, and if in important region
	double ratio = eh->p.N / max_packet_number;
	int n_new = (int)ratio;
	if(ratio>1.0 && (int)particles.size()+n_new<max_particles){
		eh->p.N /= (double)(n_new+1);
		for(int i=0; i<n_new; i++){
			#pragma omp critical
			particles.push_back(eh->p);
		}
	}

	if(eh->p.fate == moving){
		PRINT_ASSERT(eh->p.N,<,INFINITY);
		PRINT_ASSERT(eh->p.N,>,0);
	}
	if((int)particles.size()>=max_particles && verbose && rank0){
		cout << "max_particles: " << max_particles << endl;
		cout << "particles.size(): " << particles.size() << endl;
		cout << "WARNING: max_particles is too small to allow splitting." << endl;
	}
}

// choose which type of scattering event to do
void Transport::scatter(EinsteinHelper *eh) const{
	bool did_random_walk = false;

	// try to do random walk approximation in scattering-dominated diffusion regime
	if(randomwalk_sphere_size>0){

		double D = pc::c / (3.0 * eh->scatopac); // diffusion coefficient (cm^2/s)

		// if the optical depth is below our threshold, don't do random walk
		// (first pass to avoid doing lots of math)
		double Rlab_min = randomwalk_sphere_size * grid->zone_min_length(eh->z_ind);
		double Rlab_boundary = grid->zone_cell_dist(eh->p.xup,eh->z_ind);
		double Rlab = max(Rlab_min, Rlab_boundary);
		if(eh->scatopac * Rlab >= randomwalk_min_optical_depth){
			// determine maximum comoving sphere size
			const double v[3] = {eh->u[0]/eh->u[3],eh->u[1]/eh->u[3],eh->u[2]/eh->u[3]};
			double vabs = sqrt(Metric::dot_Minkowski<3>(v,v));
			double gamma = eh->u[3];

			double Rcom = 0;
			if(Rlab==0) Rcom = 0;
			else if(Rlab==INFINITY) Rcom = randomwalk_sphere_size * randomwalk_min_optical_depth / (eh->absopac>0 ? eh->absopac : eh->scatopac);
			else Rcom =  2. * Rlab / gamma / (1. + sqrt(1. + 4.*Rlab*vabs*randomwalk_max_x / (gamma*D) ) );

			// if the optical depth is below our threshold, don't do random walk
			if(eh->scatopac * Rcom >= randomwalk_min_optical_depth){
				random_walk(eh, Rcom, D);
				boundary_conditions(eh);
				did_random_walk = true;
			}
		}
	}

	// isotropic scatter if can't do random walk
	if(!did_random_walk && eh->p.fate==moving){
		// store the old direction
		double kup_tet_old[4];
		eh->coord_to_tetrad(eh->p.kup,kup_tet_old);

		// sample new direction
		double kup_tet[4];
		grid->isotropic_kup_tet(eh->nu(),kup_tet,eh->p.xup,&rangen);
		eh->tetrad_to_coord(kup_tet,eh->p.kup);

		// get the dot product between the old and new directions
		double cosTheta = eh->g.dot<3>(kup_tet,kup_tet_old) / (eh->nu() * eh->nu() * 4. * pc::pi * pc::pi / (pc::c * pc::c));
		PRINT_ASSERT(fabs(cosTheta),<=,1.0);

		// sample outgoing energy and set the post-scattered state
		if(use_scattering_kernels){
			double Nold = eh->p.N;
			sample_scattering_final_state(*eh,cosTheta);
			for(unsigned i=0; i<4; i++){
				grid->fourforce_abs[eh->z_ind][i] += (kup_tet_old[i]*Nold - kup_tet[i]*eh->p.N) * pc::h*pc::c/(2.*pc::pi);
			}
		}
	}
}



//---------------------------------------------------------------------
// Randomly select an optical depth through which a particle will move.
//---------------------------------------------------------------------
double Transport::sample_tau(ThreadRNG* rangen){
	double tau;

	do{
		tau = -log(rangen->uniform());
	} while(tau >= INFINITY);

	return tau;
}


//-------------------------------------------------------
// Initialize the CDF that determines particle dwell time
// result is D*t/(R^2)
//-------------------------------------------------------
void Transport::init_randomwalk_cdf(Lua* lua){
	int sumN = lua->scalar<int>("randomwalk_sumN");
	int npoints = lua->scalar<int>("randomwalk_npoints");
	randomwalk_max_x = lua->scalar<double>("randomwalk_max_x");
	double interpolation_order = lua->scalar<double>("randomwalk_interpolation_order");

	randomwalk_diffusion_time.resize(npoints);
	randomwalk_diffusion_time.interpolation_order = interpolation_order;
	randomwalk_xaxis.init(0,randomwalk_max_x,npoints, linear);

	#pragma omp parallel for
	for(int i=1; i<=npoints; i++){
		double sum = 0;
		double x = randomwalk_xaxis.x[i];

		for(int n=1; n<=sumN; n++){
			double tmp = 2.0 * exp(-x * (n*pc::pi)*(n*pc::pi)/3.0);
			if(n%2 == 0) tmp *= -1;
			sum += tmp;
	    }

		randomwalk_diffusion_time.set(i,1.0-sum);
	}
	randomwalk_diffusion_time.normalize();
}

//----------------------
// Do a random walk step
//----------------------
void Transport::random_walk(EinsteinHelper *eh, const double Rcom, const double D) const{
//	PRINT_ASSERT(eh->scatopac,>,0);
//	PRINT_ASSERT(eh->absopac,>=,0);
//	PRINT_ASSERT(eh->p.N,>=,0);
//	PRINT_ASSERT(eh->nu(),>=,0);
//
//	// set pointer to the current zone
//	Zone* zone;
//	zone = &(grid->z[z_ind]);
//
//	// sample the distance travelled during the random walk
//	double path_length_com = pc::c * Rcom*Rcom / D * randomwalk_diffusion_time.invert(rangen.uniform(),&randomwalk_xaxis,-1);
//	//PRINT_ASSERT(path_length_com,>=,Rcom);
//	path_length_com = max(path_length_com,Rcom);
//
//	//=================================//
//	// pick a random direction to move //
//	//=================================//
//	double Diso[3], xnew[4];
//	grid->isotropic_direction(Diso,&rangen);
//	double displacement4[4] = {Rcom*Diso[0], Rcom*Diso[1], Rcom*Diso[2], path_length_com};
//	double d3com[3] = {displacement4[0], displacement4[1], displacement4[2]};
//	LorentzHelper::transform_cartesian_4vector_c2l(zone->u, displacement4);
//	double d3lab[3] = {displacement4[0], displacement4[1], displacement4[2]};
//	xnew[0] = eh->p.xup[0] + d3lab[0];
//	xnew[1] = eh->p.xup[1] + d3lab[1];
//	xnew[2] = eh->p.xup[2] + d3lab[2];
//	xnew[3] = eh->p.xup[3] + eh->ds_lab(Rcom);
//
//	//===============================//
//	// pick a random final direction //
//	//===============================//
//	double pD[3];
//	double outward_phi = 2*pc::pi * rangen.uniform();
//	double outward_mu = rangen.uniform();
//	pD[0] = outward_mu*cos(outward_phi);
//	pD[1] = outward_mu*sin(outward_phi);
//	pD[2] = sqrt(1.0 - outward_mu*outward_mu);
//
//	// get the displacement vector polar coordinates
//	// theta_rotate is angle away from z-axis, not from xy-plane
//	Grid::normalize_Minkowski<3>(d3com);
//	double costheta_rotate = d3com[2];
//	double sintheta_rotate = sqrt(1.0 - d3com[2]*d3com[2]);
//
//	if(abs(sintheta_rotate) < TINY) pD[2] *= costheta_rotate>0 ? 1.0 : -1.0;
//	else{
//
//		// first rotate away from the z axis along y=0 (move it toward x=0)
//		Grid::normalize_Minkowski<3>(pD);
//		double pD_old[3];
//		for(int i=0; i<3; i++) pD_old[i] = pD[i];
//		pD[0] =  costheta_rotate*pD_old[0] + sintheta_rotate*pD_old[2];
//		pD[2] = -sintheta_rotate*pD_old[0] + costheta_rotate*pD_old[2];
//
//		// second rotate around the z axis, away from the x-axis
//		double cosphi_rotate = d3com[0] / sintheta_rotate;
//		double sinphi_rotate = d3com[1] / sintheta_rotate;
//		Grid::normalize_Minkowski<3>(pD);
//		for(int i=0; i<3; i++) pD_old[i] = pD[i];
//		pD[0] = cosphi_rotate*pD_old[0] - sinphi_rotate*pD_old[1];
//		pD[1] = sinphi_rotate*pD_old[0] + cosphi_rotate*pD_old[1];
//	}
//	Grid::normalize_Minkowski<3>(pD);
//	double kup_new[4];
//	kup_new[3] = lh->p_kup(com)[3];
//	kup_new[0] = kup_new[3] * pD[0];
//	kup_new[1] = kup_new[3] * pD[2];
//	kup_new[2] = kup_new[3] * pD[1];
//
//	//==============================//
//	// setup for radiation tallying //
//	//==============================//
//	LorentzHelper lhtmp = *lh;
//	lhtmp.exponential_decay = true;
//	lhtmp.set_distance<com>(path_length_com);
//	Particle fakeP = lh->particle_copy(com);
//
//	//===============================//
//	// DEPOSIT DIRECTIONAL COMPONENT //
//	//===============================//
//
//	fakeP.N = lh->p_N();
//	if(randomwalk_n_isotropic > 0)
//		fakeP.N *= Rcom / path_length_com;
//	PRINT_ASSERT(fakeP.N,>=,0);
//	fakeP.kup[0] = fakeP.kup[3] * Diso[0]; // Diso set above when choosing where to place particle
//	fakeP.kup[1] = fakeP.kup[3] * Diso[1];
//	fakeP.kup[2] = fakeP.kup[3] * Diso[2];
//	lhtmp.set_p<com>(&fakeP);
//	tally_radiation(&lhtmp,z_ind);
//
//	//=============================//
//	// DEPOSIT ISOTROPIC COMPONENT //
//	//=============================//
//
//	if(randomwalk_n_isotropic > 0){
//		fakeP.N = lh->p_N() * (path_length_com - Rcom)/path_length_com / (double)randomwalk_n_isotropic;
//		if(fakeP.N > 0) for(int ip=0; ip<randomwalk_n_isotropic; ip++){
//			// select a random direction
//			double Diso_tmp[3];
//			grid->isotropic_direction(Diso_tmp,&rangen);
//			fakeP.kup[0] = fakeP.kup[3] * Diso_tmp[0];
//			fakeP.kup[1] = fakeP.kup[3] * Diso_tmp[1];
//			fakeP.kup[2] = fakeP.kup[3] * Diso_tmp[2];
//
//			// tally the contribution
//			lhtmp.set_p<com>(&fakeP);
//			tally_radiation(&lhtmp,z_ind);
//		}
//	}
//
//	//=============================================//
//	// move the particle to the edge of the sphere //
//	//=============================================//
//	lh->set_p_xup(xnew,4);
//	lh->set_p_kup<com>(kup_new,4);
//	lh->scale_p_number( exp(-lh->abs_opac(com) * path_length_com) );
}

//-------------------------------------------------------------
// Sample outgoing neutrino direction and energy
//-------------------------------------------------------------
void Transport::sample_scattering_final_state(EinsteinHelper &eh, const double cosTheta) const{
	PRINT_ASSERT(use_scattering_kernels,>,0);
	PRINT_ASSERT(grid->scattering_delta[eh.p.s].size(),>,0);
	PRINT_ASSERT(grid->scattering_phi0[eh.p.s].size(),>,0);

	// get spatial component of directional indices
	unsigned dir_ind[NDIMS+2];
	double hyperloc[NDIMS+2];
	for(unsigned i=0; i<NDIMS; i++){
		hyperloc[i] = eh.p.xup[i];
		dir_ind[i] = eh.dir_ind[i];
	}
	dir_ind[NDIMS] = eh.dir_ind[NDIMS];
	hyperloc[NDIMS] = eh.nu();

	// get outgoing energy bin w/ rejection sampling
	unsigned igout;
	double P, phi0avg, nubar;
	do{
		igout = rangen.uniform_discrete(0, grid->nu_grid_axis.size()-1);
		dir_ind[NDIMS+1] = igout;
		nubar = 0.5 * (grid->nu_grid_axis.top[igout] - grid->nu_grid_axis.bottom(igout));
		hyperloc[NDIMS+1] = nubar;
		phi0avg = grid->scattering_phi0[eh.p.s].interpolate(hyperloc,dir_ind);
		P = phi0avg * grid->nu_grid_axis.delta(igout) / eh.scatopac;
		PRINT_ASSERT(P,<=,1.0);
	} while(rangen.uniform() > P);

	// uniformly sample within zone
	unsigned global_index = grid->scattering_phi0[eh.p.s].direct_index(dir_ind);
	double out_nu = rangen.uniform(grid->nu_grid_axis.bottom(igout), grid->nu_grid_axis.top[igout]);
	eh.scale_p_frequency(out_nu/eh.nu());
	double phi_interpolated = grid->scattering_phi0[eh.p.s].dydx[global_index][NDIMS+1][0]*(out_nu - nubar) + phi0avg;
	eh.p.N *= phi_interpolated / phi0avg;

	// bias outgoing direction to be isotropic. Very inefficient for large values of delta.
	hyperloc[NDIMS+1] = out_nu;
	double delta = grid->scattering_delta[eh.p.s].interpolate(hyperloc,dir_ind);
	PRINT_ASSERT(fabs(delta),<,3.0);
	if(fabs(delta)<=1.0) eh.p.N *= 1.0 + delta*cosTheta;
	else{
		double b = 2.*fabs(delta) / (3.-fabs(delta));
		if(delta>1.0) eh.p.N *= pow(1.+cosTheta, b);
		else          eh.p.N *= pow(1.-cosTheta, b);
	}
}
