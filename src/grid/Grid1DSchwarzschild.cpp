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

#include <mpi.h>
#include <fstream>
#include "Transport.h"
#include "Grid1DSchwarzschild.h"
#include "global_options.h"

using namespace std;
namespace pc = physical_constants;

//------------------------------------------------------------
// initialize the zone geometry from model file
//------------------------------------------------------------
void Grid1DSchwarzschild::read_model_file(Lua* lua)
{
	// verbocity
	int my_rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &my_rank );
	const int rank0 = (my_rank == 0);
	if(rank0) cout << "#   Reading 1D model file" << endl;

	// open up the model file, complaining if it fails to open
	string model_file = lua->scalar<string>("model_file");
	ifstream infile;
	infile.open(model_file.c_str());
	if(infile.fail()){
		cout << "Error: can't read the model file." << model_file << endl;
		exit(4);
	}

	// geometry of model
	infile >> grid_type;
	if(grid_type != "1D_sphere"){
		cout << "Error: grid_type parameter disagrees with the model file." << endl;
	}

	// number of zones
	int n_zones;
	infile >> n_zones;
	PRINT_ASSERT(n_zones,>,0);
	z.resize(n_zones);
	r_out.resize(n_zones);

	// read zone properties
	infile >> r_out.min;
	PRINT_ASSERT(r_out.min,>=,0);
	for(int z_ind=0; z_ind<n_zones; z_ind++)
	{
		infile >> r_out[z_ind];
		infile >> z[z_ind].rho;
		infile >> z[z_ind].T;
		infile >> z[z_ind].Ye;
		z[z_ind].H_vis = 0;
		z[z_ind].u[0] = 0;
		z[z_ind].u[1] = 0;
		z[z_ind].u[2] = 0;
		PRINT_ASSERT(r_out[z_ind],>,(z_ind==0 ? r_out.min : r_out[z_ind-1]));
		PRINT_ASSERT(z[z_ind].rho,>=,0);
		PRINT_ASSERT(z[z_ind].T,>=,0);
		PRINT_ASSERT(z[z_ind].Ye,>=,0);
		PRINT_ASSERT(z[z_ind].Ye,<=,1.0);
		PRINT_ASSERT(ARRSIZE(z[z_ind].u),==,3);
	}

	infile.close();
}


//------------------------------------------------------------
// Return the zone index containing the position x
//------------------------------------------------------------
int Grid1DSchwarzschild::zone_index(const double x[3], const int xsize) const
{
	PRINT_ASSERT(z.size(),>,0);
	PRINT_ASSERT(xsize,==,3);
	double r = sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
	PRINT_ASSERT(r,>,0);

	// check if off the boundaries
	if(r < r_out.min             ) return -1;
	if(r >= r_out[r_out.size()-1] ) return -2;

	// find in zone array using stl algorithm upper_bound and subtracting iterators
	int z_ind = r_out.locate(r);
	PRINT_ASSERT(z_ind,>=,0);
	if(z_ind>=(int)z.size()){
		cout << z_ind << endl;
		cout << z.size() << endl;
		cout << r << endl;
		cout << r_out[0] << endl;
	}
	PRINT_ASSERT(z_ind,<,(int)z.size());
	return z_ind;
}


//------------------------------------------------------------
// return volume of zone z_ind
//------------------------------------------------------------
double  Grid1DSchwarzschild::zone_lab_volume(const int z_ind) const
{
	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)z.size());
	double r0 = (z_ind==0 ? r_out.min : r_out[z_ind-1]);
	double vol = 4.0*pc::pi/3.0*(r_out[z_ind]*r_out[z_ind]*r_out[z_ind] - r0*r0*r0);
	PRINT_ASSERT(vol,>=,0);
	return vol;
}


//------------------------------------------------------------
// return length of zone
//------------------------------------------------------------
double  Grid1DSchwarzschild::zone_min_length(const int z_ind) const
{
	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)z.size());
	double r0 = (z_ind==0 ? r_out.min : r_out[z_ind-1]);
	double min_len = r_out[z_ind] - r0;
	PRINT_ASSERT(min_len,>=,0);
	return min_len;
}


// ------------------------------------------------------------
// find the coordinates of the zone in geometrical coordinates
// ------------------------------------------------------------
void Grid1DSchwarzschild::zone_coordinates(const int z_ind, double r[1], const int rsize) const{
	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)z.size());
	PRINT_ASSERT(rsize,==,dimensionality());
	r[0] = 0.5*(r_out[z_ind]+r_out.bottom(z_ind));
	PRINT_ASSERT(r[0],>,0);
	PRINT_ASSERT(r[0],<,r_out[r_out.size()-1]);
}


//-------------------------------------------
// get directional indices from zone index
//-------------------------------------------
void Grid1DSchwarzschild::zone_directional_indices(const int z_ind, int dir_ind[1], const int size) const
{
	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)z.size());
	PRINT_ASSERT(size,==,dimensionality());
	dir_ind[0] = z_ind;
}


//------------------------------------------------------------
// sample a random position within the spherical shell
//------------------------------------------------------------
void Grid1DSchwarzschild::cartesian_sample_in_zone
(const int z_ind, const double rand[3], const int randsize, double x[3], const int xsize) const
{
	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)z.size());
	PRINT_ASSERT(randsize,==,3);
	PRINT_ASSERT(xsize,==,3);

	// inner and outer radii of shell
	double r0 = (z_ind==0 ? r_out.min : r_out[z_ind-1]);
	double r1 = r_out[z_ind];

	// sample radial position in shell using a probability integral transform
	double radius = pow( rand[0]*(r1*r1*r1 - r0*r0*r0) + r0*r0*r0, 1./3.);
	PRINT_ASSERT(radius,>=,r0*(1.-tiny));
	PRINT_ASSERT(radius,<=,r1*(1.+tiny));
	if(radius<r0) radius = r0;
	if(radius>r1) radius = r1;

	// random spatial angles
	double mu  = 1 - 2.0*rand[1];
	double phi = 2.0*pc::pi*rand[2];
	double sin_theta = sqrt(1 - mu*mu);

	// set the double 3-d coordinates
	x[0] = radius*sin_theta*cos(phi);
	x[1] = radius*sin_theta*sin(phi);
	x[2] = radius*mu;
}


//------------------------------------------------------------
// get the velocity vector
//------------------------------------------------------------
void Grid1DSchwarzschild::cartesian_velocity_vector(const double x[3], const int xsize, double v[3], const int vsize, int z_ind) const
{
	PRINT_ASSERT(xsize,==,3);
	PRINT_ASSERT(vsize,==,3);
	if(z_ind < 0) z_ind = zone_index(x,xsize);
	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)z.size());

	// radius in zone
	double r = sqrt(dot(x,x,xsize));

	// assuming radial velocity (may want to interpolate here)
	// (the other two components are ignored and mean nothing)
	PRINT_ASSERT(ARRSIZE(z[z_ind].u),==,3);
	v[0] = x[0]/r*z[z_ind].u[0];
	v[1] = x[1]/r*z[z_ind].u[0];
	v[2] = x[2]/r*z[z_ind].u[0];

	// check for pathological case
	if (r == 0)
	{
		v[0] = 0;
		v[1] = 0;
		v[2] = 0;
	}

	PRINT_ASSERT(dot(v,v,vsize),<=,pc::c*pc::c);
}


//------------------------------------------------------------
// Write the grid information out to a file
//------------------------------------------------------------
void Grid1DSchwarzschild::write_rays(const int iw) const
{
	// this is a 1D grid, so the function is exactly the same
	// as write_zones
}


//------------------------------------------------------------
// Reflect off the outer boundary
//------------------------------------------------------------
void Grid1DSchwarzschild::reflect_outer(LorentzHelper *lh) const{
	const Particle *p = lh->particle_readonly(lab);
	double r0 = (r_out.size()>1 ? r_out[r_out.size()-2] : r_out.min);
	double rmax = r_out[r_out.size()-1];
	double dr = rmax - r0;
	double R = radius(p->x,3);
	double x_dot_d = p->x[0]*p->D[0] + p->x[1]*p->D[1] + p->x[2]*p->D[2];
	double velDotRhat = x_dot_d / R;

	PRINT_ASSERT( fabs(R - r_out[r_out.size()-1]),<,tiny*dr);

	// invert the radial component of the velocity
	double D[3];
	D[0] -= 2.*velDotRhat * p->x[0]/R;
	D[1] -= 2.*velDotRhat * p->x[1]/R;
	D[2] -= 2.*velDotRhat * p->x[2]/R;
	normalize(D,3);
	lh->set_p_D<lab>(D,3);

	// put the particle just inside the boundary
	double newR = rmax - tiny*dr;
	double x[3];
	x[0] = p->x[0]/R*newR;
	x[1] = p->x[1]/R*newR;
	x[2] = p->x[2]/R*newR;
	lh->set_p_x(x,3);

	// must be inside the boundary, or will get flagged as escaped
	PRINT_ASSERT(zone_index(x,3),>=,0);
}

//------------------------------------------------------------
// Reflect off symmetry axis
//------------------------------------------------------------
void Grid1DSchwarzschild::symmetry_boundaries(LorentzHelper *lh) const{
// not implemented - does nothing
}

//------------------------------------------------------------
// Find distance to outer boundary (less a tiny bit)
// negative distance means inner boundary
//------------------------------------------------------------
double Grid1DSchwarzschild::lab_dist_to_boundary(const LorentzHelper *lh) const{
	const Particle *p = lh->particle_readonly(lab);
	// Theta = angle between radius vector and direction (Pi if outgoing)
	// Phi   = Pi - Theta (angle on the triangle) (0 if outgoing)
	double Rout  = r_out[r_out.size()-1];
	double Rin   = r_out.min;
	double r  = radius(p->x,3);
	double x_dot_d = p->x[0]*p->D[0] + p->x[1]*p->D[1] + p->x[2]*p->D[2];
	double mu = x_dot_d / r;
	double d_outer_boundary = numeric_limits<double>::infinity();
	double d_inner_boundary = numeric_limits<double>::infinity();
	PRINT_ASSERT(r,<,Rout);
	PRINT_ASSERT(zone_index(p->x,3),>=,-1);

	// distance to inner boundary
	if(r >= Rin){
		double radical = r*r*(mu*mu-1.0) + Rin*Rin;
		if(Rin>0 && mu<0 && radical>=0){
			d_inner_boundary = -r*mu - sqrt(radical);
			PRINT_ASSERT(d_inner_boundary,<=,sqrt(Rout*Rout-Rin*Rin)*(1.0+tiny));
		}
	}
	else{
		d_inner_boundary = -r*mu + sqrt(r*r*(mu*mu-1.0) + Rin*Rin);
		PRINT_ASSERT(d_inner_boundary,<=,2.*Rin);
	}
	if(d_inner_boundary<=0 && fabs(d_inner_boundary/Rin)<tiny*(r_out[0]-Rin)) d_inner_boundary = tiny*(r_out[0]-Rin);
	PRINT_ASSERT(d_inner_boundary,>,0);

	// distance to outer boundary
	d_outer_boundary = -r*mu + sqrt(r*r*(mu*mu-1.0) + Rout*Rout);
	if(d_outer_boundary<=0 && fabs(d_outer_boundary/Rin)<tiny*(Rout-r_out[r_out.size()-1])) d_outer_boundary = tiny*(Rout-r_out[r_out.size()-1]);
	PRINT_ASSERT(d_outer_boundary,>,0);
	PRINT_ASSERT(d_outer_boundary,<=,2.*Rout);

	// make sure the particle ends up in a reasonable place
	return min(d_inner_boundary, d_outer_boundary);
}

double Grid1DSchwarzschild::zone_radius(const int z_ind) const{
	PRINT_ASSERT(z_ind,>=,0);
	PRINT_ASSERT(z_ind,<,(int)z.size());
	return r_out[z_ind];
}

//-----------------------------
// Dimensions of the grid
//-----------------------------
void Grid1DSchwarzschild::dims(hsize_t dims[1], const int size) const{
	PRINT_ASSERT(size,==,dimensionality());
	dims[0] = r_out.size();
}

//----------------------------------------------------
// Write the coordinates of the grid points to the hdf5 file
//----------------------------------------------------
void Grid1DSchwarzschild::write_hdf5_coordinates(H5::H5File file) const
{
	// useful quantities
	H5::DataSet dataset;
	H5::DataSpace dataspace;
	float tmp[r_out.size()+1];

	// get dimensions
	hsize_t coord_dims[1];
	dims(coord_dims,1);
	for(unsigned i=0; i<1; i++) coord_dims[i]++; //make room for min value

	// write coordinates
	dataspace = H5::DataSpace(1,&coord_dims[0]);
	dataset = file.createDataSet("r(cm)",H5::PredType::IEEE_F32LE,dataspace);
	tmp[0] = r_out.min;
	for(unsigned i=1; i<r_out.size()+1; i++) tmp[i] = r_out[i-1];
	dataset.write(&tmp[0],H5::PredType::IEEE_F32LE);
	dataset.close();
}