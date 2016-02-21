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

#ifndef _TRANSPORT_H
#define _TRANSPORT_H
#include <vector>
#include "Particle.h"
#include "Lua.h"
#include "CDFArray.h"
#include "ThreadRNG.h"

class Species;
class Grid;
class SpectrumArray;
enum ParticleEvent {interact, zoneEdge, boundary};

class Transport
{

private:

	// this species' list of particles
	std::vector<Particle> particles;

	// MPI stuff
	int MPI_nprocs;
	int MPI_myID;
	void reduce_radiation();
	void synchronize_gas();
	std::vector<unsigned> my_zone_end;

	// subroutine for calculating timescales
	void calculate_timescales() const;
	void calculate_annihilation() const;

	// main function to emit particles
	void emit_particles();

	// emit from where?
	void emit_inner_source();
	void emit_zones();
	void emit_inner_source_by_bin();
	void emit_zones_by_bin();

	// what kind of particle to create?
	void create_surface_particle(const double Ep, const int s=-1, const int g=-1);
	void create_thermal_particle(const int zone_index, const double Ep, const int s=-1, const int g=-1);

	// species sampling functions
	int sample_core_species() const;
	void sample_zone_species(Particle *p, int zone_index) const;

	// transformation functions
	double comoving_dt(const int z_ind) const;
	double dshift_comoving_to_lab   (const Particle* p, const int z_ind=-1) const;
	double dshift_lab_to_comoving   (const Particle* p, const int z_ind=-1) const;
	void   transform_comoving_to_lab(Particle* p, const int z_ind=-1) const;
	void   transform_lab_to_comoving(Particle* p, const int z_ind=-1) const;
	void lab_opacity(const Particle *p, const int z_ind, double *lab_opac, double *abs_frac, double *dshift_l2c) const;

	// propagate the particles
	void propagate_particles();
	void propagate(Particle* p);
	void move(Particle* p, const double lab_d, const double lab_opac);
	void tally_radiation(const Particle* p, const int z_ind, const double dshift_l2c, const double lab_d, const double lab_opac, const double abs_frac) const;
	void reset_radiation();
	void which_event(Particle* p,const int z_ind, const double lab_opac, double* d_smallest, ParticleEvent *event) const;
	void event_boundary(Particle* p, const int z_ind) const;
	void event_interact(Particle* p, const int z_ind, const double abs_frac,const double lab_opac);
	void isotropic_scatter(Particle* p) const;
	void re_emit(Particle* p, const int z_ind) const;
	void window(Particle* p, const int z_ind);

	// solve for temperature and Ye (if steady_state)
	double damping;
	int    brent_itmax;
	double brent_solve_tolerance;
	void   solve_eq_zone_values();
	void   normalize_radiative_quantities();
	double brent_method(const int zone_index, double (*eq_function)(double, void*), const double min, const double max);

	// update temperature and Ye (if !steady_state)
	void update_zone_quantities();

	// stored minimum and maximum values to assure safety
	int max_particles;

	// simulation parameters
	double step_size;
	int    do_annihilation;
	int    radiative_eq;
	int    rank0;

	// output parameters
	int write_zones_every;
	int write_rays_every;
	int write_spectra_every;

	// global radiation quantities
	double particle_total_energy;
	double particle_fluid_abs_energy;
	double particle_core_abs_energy;
	double particle_escape_energy;

	// check parameters
	void check_parameters() const;

	// check quality
	int number_of_bins() const;

public:

	Transport();

	int verbose;

	int    solve_T;
	int    solve_Ye;
	double T_min,  T_max;
	double Ye_min, Ye_max;
	double rho_min, rho_max;

	// arrays of species
	std::vector<Species*> species_list;

	// pointer to grid
	Grid *grid;

	// biasing
	// minimum neutrino packet energy
	double min_packet_energy;
	double max_packet_energy;
	double importance_bias;
	double min_importance;
	int bias_path_length;
	double max_path_length_boost;
	void sample_tau(Particle* p, const double lab_opac, const double abs_frac);

	// items for core emission
	double r_core;
	int n_emit_core;
	int n_emit_core_per_bin;
	double core_lum_multiplier;
	int core_emit_method;
	CDFArray core_species_luminosity;
	void init_core(const double r_core, const double T_core, const double munue_core);
	void init_core(const double r_core, const std::vector<double>& T_core, const std::vector<double>& mu_core, const std::vector<double>& L_core);

	// items for zone emission
	int do_visc;
	int do_relativity;
	int n_emit_zones;
	int n_emit_zones_per_bin;
	double visc_specific_heat_rate;

	// how many times do we emit+propagate each timestep?
	int emissions_per_timestep;
	int do_emit_by_bin;

	// global radiation quantities
	std::vector<double> L_core_lab;
	std::vector<double> L_net_lab;
	std::vector<double> L_net_esc;
	std::vector<double> E_avg_lab;
	std::vector<double> E_avg_esc;
	std::vector<double> N_net_lab;
	std::vector<double> N_net_esc;
	std::vector<long> n_active;
	std::vector<long> n_escape;
	double annihil_rho_cutoff;


	// reflect off the outer boundary?
	int reflect_outer;

	// random number generator
	mutable ThreadRNG rangen;

	// set things up
	void   init(Lua* lua);

	// in-simulation functions to be used by main
	void step();
	void write(const int it) const;
	int  total_particles() const;
	void write_rays(const int it);
	static std::string filename(const char* filebase, const int iw, const char* suffix);
	static double lorentz_factor(const double v[3], const int vsize);
	static double dot(const std::vector<double>& a, const std::vector<double>& b);
	static double dot(const std::vector<double>& a, const double b[], const int size);
	static double dot(const double a[], const double b[], const int size);
	static void normalize(std::vector<double>& a);
	static void normalize(double a[], const int size);
	static double mean_mass(const double Ye);
	double importance(const double abs_opac, const double scat_opac, const double dx) const;


	// per-zone luminosity functions
	double zone_comoving_visc_heat_rate(const int zone_index) const;
	double zone_comoving_therm_emit_energy (const int zone_index) const;
	double zone_comoving_therm_emit_leptons(const int zone_index) const;
	double zone_comoving_biased_therm_emit_energy(const int z_ind) const;
	double bin_comoving_therm_emit_energy(const int z_ind, const int s, const int g) const;


};

#endif
