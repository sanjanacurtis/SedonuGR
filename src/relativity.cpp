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

#include "transport.h"
#include "particle.h"
#include "grid_general.h"
#include "global_options.h"

// lorentz factor ("gamma")
// v_rel = v_newframe - v_oldframe
double transport::lorentz_factor(const vector<double>& v){
	PRINT_ASSERT(v.size(),<=,3);
	PRINT_ASSERT(dot(v,v),<,pc::c*pc::c);
	double beta2 = dot(v,v) / (pc::c*pc::c);
	double lfac = 1.0 / sqrt(1.0 - beta2);
	PRINT_ASSERT(lfac,>=,1.0);
	return lfac;
}

// dot product of v_rel and relativistic particle direction
// v_rel = v_newframe - v_oldframe
// D = direction vector of relativistic particle in old frame
double transport::dot(const vector<double>& a, const vector<double>& b){
	PRINT_ASSERT(a.size(),>,0);
	PRINT_ASSERT(b.size(),>,0);
	PRINT_ASSERT(a.size(),==,b.size());
	double product = 0;
	for(unsigned i=0; i<a.size(); i++) product += a[i]*b[i];
	return product;
}
double transport::dot(const vector<double>& a, const double* b, const int size){
	PRINT_ASSERT(a.size(),>,0);
	PRINT_ASSERT(size,>,0);
	PRINT_ASSERT(a.size(),==,size);
	double product = 0;
	for(unsigned i=0; i<a.size(); i++) product += a[i]*b[i];
	return product;
}
double transport::dot(const double* a, const double* b, const int size){
	PRINT_ASSERT(size,>,0);
	double product = 0;
	for(unsigned i=0; i<size; i++) product += a[i]*b[i];
	return product;
}

// normalize a vector
void transport::normalize(vector<double>& a){
	PRINT_ASSERT(a.size(),>,0);
	double magnitude = sqrt(dot(a,a));
	for(unsigned i=0; i<a.size(); i++) a[i] /= magnitude;
}
void transport::normalize(double* a,const int size){
	PRINT_ASSERT(size,>,0);
	double magnitude = sqrt(dot(a,a,size));
	for(unsigned i=0; i<sizeof(a)/sizeof(a[0]); i++) a[i] /= magnitude;
}

// v_dot_d is the dot product of the relative velocity and the relativistic particle's direction
double doppler_shift(const double gamma, const double vdd){
	PRINT_ASSERT(gamma,>,0);
	double dshift = gamma * (1.0 - vdd/pc::c);
	PRINT_ASSERT(dshift,>,0);
	return dshift;
}

// apply a lorentz transform to the particle
// v = v_newframe - v_oldframe
void lorentz_transform(particle* p, const vector<double> v){
	// check input
	PRINT_ASSERT(v.size(),==,3);
	PRINT_ASSERT(p->nu,>,0);
	PRINT_ASSERT(p->e,>,0);

	// calculate the doppler shift, v dot D, and lorentz factors
	double gamma = transport::lorentz_factor(v);
	double vdd = transport::dot(v, p->D);
	double dshift = doppler_shift(gamma, vdd);

	// transform the 0th component (energy and frequency)
	p->e  *= dshift;
	p->nu *= dshift;

	// transform the 1-3 components (direction)
	// See Mihalas & Mihalas eq 89.8
	p->D[0] = 1.0/dshift * (p->D[0] - gamma*v[0]/pc::c * (1 - gamma*vdd/pc::c/(gamma+1)) );
	p->D[1] = 1.0/dshift * (p->D[1] - gamma*v[1]/pc::c * (1 - gamma*vdd/pc::c/(gamma+1)) );
	p->D[2] = 1.0/dshift * (p->D[2] - gamma*v[2]/pc::c * (1 - gamma*vdd/pc::c/(gamma+1)) );
	transport::normalize(p->D);

	// sanity checks
	PRINT_ASSERT(p->e,>,0);
	PRINT_ASSERT(p->nu,>,0);
	PRINT_ASSERT(dshift,>,0);
}


//------------------------------------------------------------
// get the doppler shift when moving from frame_to_frame
// does not change any particle properties
//------------------------------------------------------------
double transport::dshift_comoving_to_lab(const particle* p, const int z_ind) const
{
	if(!do_relativity) return 1.0;

	vector<double> v;
	grid->cartesian_velocity_vector(p->x,v,z_ind); // v_comoving - v_lab

	// new frame is lab frame. old frame is comoving frame.
	// v_rel = v_lab - v_comoving  --> v must flip sign.
	v[0] *= -1;
	v[1] *= -1;
	v[2] *= -1;

	double gamma = lorentz_factor(v);
	double vdd = dot(v, p->D);
	double dshift = doppler_shift(gamma,vdd);
	PRINT_ASSERT(dshift,>,0);
	return dshift;
}

double transport::dshift_lab_to_comoving(const particle* p, const int z_ind) const
{
	if(!do_relativity) return 1.0;

	vector<double> v;
	grid->cartesian_velocity_vector(p->x,v,z_ind); // v_comoving - v_lab

	// new frame is comoving frame. old frame is lab frame.
	// v_rel = v_comoving - v_lab  -->  v keeps its sign

	double gamma = lorentz_factor(v);
	double vdd = dot(v, p->D);
	double dshift = doppler_shift(gamma,vdd);
	PRINT_ASSERT(dshift,>,0);
	return dshift;
}


//------------------------------------------------------------
// do a lorentz transformation; modifies the energy, frequency
// and direction vector of the particle
//------------------------------------------------------------
void transport::transform_comoving_to_lab(particle* p, const int z_ind) const
{
	if(!do_relativity) return;

	vector<double> v;
	grid->cartesian_velocity_vector(p->x,v,z_ind); // v_comoving - v_lab

	// new frame is lab frame. old frame is comoving frame.
	// v_rel = v_lab - v_comoving  --> v must flip sign.
	v[0] *= -1;
	v[1] *= -1;
	v[2] *= -1;

	lorentz_transform(p,v);
}

void transport::transform_lab_to_comoving(particle* p, const int z_ind) const
{
	if(!do_relativity) return;

	vector<double> v;
	grid->cartesian_velocity_vector(p->x,v,z_ind); // v_comoving - v_lab

	// new frame is lab frame. old frame is comoving frame.
	// v_rel = v_comoving - v_lab  --> v keeps its sign.

	lorentz_transform(p,v);
}

double transport::comoving_dt(const int z_ind) const{
	if(!do_relativity) return 1.0;

	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)grid->z.size());
	return 1.0 / lorentz_factor(grid->z[z_ind].v); // assume lab_dt=1.0
}
