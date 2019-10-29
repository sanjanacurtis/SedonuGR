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
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include "LuaRead.h"
#include "Transport.h"
#include "Species.h"
#include "Grid.h"
#include "nulib_interface.h"
#include "global_options.h"
#include "MomentSpectrumArray.h"

using namespace std;
namespace pc = physical_constants;

const size_t NF=4;


	// set up the transport module (includes the grid)
	class testTransport : public Transport{
	public:
		void move(EinsteinHelper *eh, double* ct){
			PRINT_ASSERT(eh->ds_com,>=,0);
			PRINT_ASSERT(eh->N,>,0);
			PRINT_ASSERT(abs(eh->g.dot<4>(eh->kup,eh->kup)) / (eh->kup[3]*eh->kup[3]), <=, TINY);

			// save old values
			Tuple<double,4> old_kup = eh->kup;

			// convert ds_com into dlambda
			double dlambda = eh->ds_com / eh->kup_tet[3];
			PRINT_ASSERT(dlambda,>=,0);

			// get 2nd order x, 1st order estimate for k
			Tuple<double,4> order1 = old_kup * dlambda;
			for(size_t i=0; i<4; i++)
				eh->xup[i] += order1[i];
			if(DO_GR){
				Tuple<double,4> dk_dlambda = grid->dk_dlambda(*eh);
				Tuple<double,4> order2 = dk_dlambda * dlambda*dlambda * 0.5;
				eh->kup = old_kup + dk_dlambda * dlambda;
				for(size_t i=0; i<4; i++)
					eh->xup[i] += (abs(order2[i]/order1[i])<1. ? order2[i] : 0);
			}

			// get new background data
			update_eh_background(eh);
			if(eh->fate==moving)
				update_eh_k_opac(eh);

			double ds_com_new = dlambda*eh->kup_tet[3];
			*ct += (ds_com_new + eh->ds_com) / 2.;
		}
	};

class TrajectoryData{
public:
	vector<double> ct, Ecom_Elab, Elab_Elab0, TMeV, Ye, rho;
	vector<vector<double> > x;
	vector< vector< vector<double> > > Ndens, Fdens, Pdens;
	double nulab0;

	TrajectoryData(int NS, int NE){
		nulab0 = -1.e99;
		ct.resize(0);
		Ecom_Elab.resize(0);
		Elab_Elab0.resize(0);
		TMeV.resize(0);
		Ye.resize(0);
		rho.resize(0);
		x.resize(4);

		Ndens.resize(NS);
		Fdens.resize(NS);
		Pdens.resize(NS);

		for(int s=0; s<NS; s++){
			Ndens[s].resize(NE);
			Fdens[s].resize(NE);
			Pdens[s].resize(NE);
			for(int g=0; g<NE; g++){
				Ndens[s][g].resize(0);
				Fdens[s][g].resize(0);
				Pdens[s][g].resize(0);
			}
		}
	}
};

void append_data(const Transport* sim, const EinsteinHelper* eh, double ct, TrajectoryData* td){
	double nulab = -eh->g.ndot(eh->kup);
	if(td->ct.size()==0) td->nulab0 = nulab;

	td->ct.push_back(ct);
	td->rho.push_back(sim->grid->rho.interpolate(eh->icube_vol));
	td->TMeV.push_back(sim->grid->T.interpolate(eh->icube_vol) * pc::k_MeV);
	td->Ye.push_back(sim->grid->Ye.interpolate(eh->icube_vol));
	td->Ecom_Elab.push_back(-eh->kup_tet[3]/eh->g.ndot(eh->kup));
	td->Elab_Elab0.push_back(nulab/td->nulab0);
	for(size_t i=0; i<4; i++){
		td->x[i].push_back(eh->xup[i]);
	}

	// get stuff ready to interpolate moments
	Tuple<double, 20> moments;
	double icube_x[4] = {eh->xup[0],     eh->xup[1],     eh->xup[2],     0};
	size_t dir_ind[4] = {eh->dir_ind[0], eh->dir_ind[1], eh->dir_ind[2], 0};
	double khat_tet[3] = {
			eh->kup_tet[0]/eh->kup_tet[3],
			eh->kup_tet[1]/eh->kup_tet[3],
			eh->kup_tet[2]/eh->kup_tet[3]};

	for(unsigned s=0; s<sim->grid->distribution.size(); s++){
		MomentSpectrumArray<3>* dist = (MomentSpectrumArray<3>*)sim->grid->distribution[s];
		size_t n_species;
		if(s<=1) n_species = 1;
		else{
			if(sim->grid->distribution.size()==3) n_species = 4;
			else if(sim->grid->distribution.size()==4) n_species=2;
			else{
				n_species=0;
				assert(0);
			}
		}

		for(unsigned g=0; g<sim->grid->nu_grid_axis.size(); g++){
			// set the interpolation cube
			dir_ind[3] = g;
			icube_x[3] = sim->grid->nu_grid_axis.mid[g];

			moments = dist->interpolate(icube_x, dir_ind) / (pc::h * icube_x[3]); // convert to number density
			td->Ndens[s][g].push_back(
					moments[0] / n_species);
			td->Fdens[s][g].push_back((
					moments[1] * khat_tet[0] +
					moments[2] * khat_tet[1] +
					moments[3] * khat_tet[2] ) / n_species);
			td->Pdens[s][g].push_back((
					moments[4] * khat_tet[0]*khat_tet[0]*1. +  // xx
					moments[5] * khat_tet[0]*khat_tet[1]*2. +  // xy
					moments[6] * khat_tet[0]*khat_tet[2]*2. +  // xz
					moments[7] * khat_tet[1]*khat_tet[1]*1. +  // yy
					moments[8] * khat_tet[1]*khat_tet[2]*2. +  // yz
					moments[9] * khat_tet[2]*khat_tet[2]*1. ) / n_species); // zz
		}
	}
	cout << "n=" << td->ct.size() << endl;
}

hid_t create_file(string filename, const TrajectoryData& td, const testTransport& sim){
	hid_t file = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	hid_t dset, file_space, mem_space;
	hsize_t ndims;
	hsize_t count[3]  = {1,1,1};
	hsize_t stride[3] = {1,1,1};

	// 1D stuff
	ndims = 1;
	hsize_t dims1[1] = {td.ct.size()};
	hid_t space1d = H5Screate_simple(ndims, dims1, dims1);

	dset = H5Dcreate(file, "ct(cm)", H5T_NATIVE_DOUBLE, space1d, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	H5Dwrite(dset, H5T_NATIVE_DOUBLE, space1d, space1d, H5P_DEFAULT, &td.ct[0]);

	dset = H5Dcreate(file, "rho(g|ccm)", H5T_NATIVE_DOUBLE, space1d, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	H5Dwrite(dset, H5T_NATIVE_DOUBLE, space1d, space1d, H5P_DEFAULT, &td.rho[0]);

	dset = H5Dcreate(file, "T(MeV)", H5T_NATIVE_DOUBLE, space1d, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	H5Dwrite(dset, H5T_NATIVE_DOUBLE, space1d, space1d, H5P_DEFAULT, &td.TMeV[0]);

	dset = H5Dcreate(file, "Ye", H5T_NATIVE_DOUBLE, space1d, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	H5Dwrite(dset, H5T_NATIVE_DOUBLE, space1d, space1d, H5P_DEFAULT, &td.Ye[0]);

	dset = H5Dcreate(file, "Ecom_Elab", H5T_NATIVE_DOUBLE, space1d, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	H5Dwrite(dset, H5T_NATIVE_DOUBLE, space1d, space1d, H5P_DEFAULT, &td.Ecom_Elab[0]);

	dset = H5Dcreate(file, "Elab_Elab0", H5T_NATIVE_DOUBLE, space1d, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	H5Dwrite(dset, H5T_NATIVE_DOUBLE, space1d, space1d, H5P_DEFAULT, &td.Elab_Elab0[0]);

	// energy grid
	double Nnu = sim.grid->nu_grid_axis.size();
	dims1[0] = Nnu;
	space1d = H5Screate_simple(ndims, dims1, dims1);
	vector<double> tmp(Nnu);

	dset = H5Dcreate(file, "Ecom(erg)", H5T_NATIVE_DOUBLE, space1d, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	for(size_t i=0; i<Nnu; i++) tmp[i] = sim.grid->nu_grid_axis.mid[i] * pc::h;
	H5Dwrite(dset, H5T_NATIVE_DOUBLE, space1d, space1d, H5P_DEFAULT, &tmp[0]);

	dset = H5Dcreate(file, "Etopcom(erg)", H5T_NATIVE_DOUBLE, space1d, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	for(size_t i=0; i<Nnu; i++) tmp[i] = sim.grid->nu_grid_axis.top[i] * pc::h;
	H5Dwrite(dset, H5T_NATIVE_DOUBLE, space1d, space1d, H5P_DEFAULT, &tmp[0]);

	// coordinates
	ndims = 2;
	hsize_t fdims2[2] = {4, td.x[0].size()};
	hsize_t mdims2[2] = {1, td.x[0].size()};
	file_space = H5Screate_simple(ndims, fdims2, fdims2);
	mem_space  = H5Screate_simple(ndims, mdims2, mdims2);
	dset = H5Dcreate(file, "x(cm)", H5T_NATIVE_DOUBLE, file_space, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	for(size_t i=0; i<4; i++){
		hsize_t start[3] = {i, 0};
		H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start, count, stride, mdims2);
		H5Dwrite(dset, H5T_NATIVE_DOUBLE, mem_space, file_space, H5P_DEFAULT, &td.x[i][0]);
	}

	// moment stuff
	ndims = 3;
	hsize_t fdims3[3] = {NF, td.Ndens[0].size(), td.Ndens[0][0].size()};
	hsize_t mdims3[3] = {1,  1,                  td.Ndens[0][0].size()};
	file_space = H5Screate_simple(ndims, fdims3, fdims3);
	mem_space  = H5Screate_simple(ndims, mdims3, mdims3);

	hid_t dsetN = H5Dcreate(file, "Ndens(1|ccm)", H5T_NATIVE_DOUBLE, file_space, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	hid_t dsetF = H5Dcreate(file, "Fdens(1|ccm)", H5T_NATIVE_DOUBLE, file_space, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
	hid_t dsetP = H5Dcreate(file, "Pdens(1|ccm)", H5T_NATIVE_DOUBLE, file_space, H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);

	// have to write in chunks because vectors of vectors are not contiguous in memory
	for(size_t s=0; s<NF; s++){
		for(size_t g=0; g<Nnu; g++){
			hsize_t start[3] = {s, g, 0};
			H5Sselect_hyperslab(file_space, H5S_SELECT_SET, start, count, stride, mdims3);
			size_t s_data = min(s, td.Ndens.size()-1); // copy heavy-lepton data to regular and anti data in file
			size_t nr = td.Ndens[s_data][g].size();
			vector<double> tmp(nr,0);

			for(size_t ir=0; ir<nr; ir++) tmp[ir] = td.Ndens[s_data][g][ir] / sim.species_list[s_data]->num_species;
			H5Dwrite(dsetN, H5T_NATIVE_DOUBLE, mem_space, file_space, H5P_DEFAULT, &tmp[0]);

			for(size_t ir=0; ir<nr; ir++) tmp[ir] = td.Fdens[s_data][g][ir] / sim.species_list[s_data]->num_species;
			H5Dwrite(dsetF, H5T_NATIVE_DOUBLE, mem_space, file_space, H5P_DEFAULT, &tmp[0]);

			for(size_t ir=0; ir<nr; ir++) tmp[ir] = td.Pdens[s_data][g][ir] / sim.species_list[s_data]->num_species;
			H5Dwrite(dsetP, H5T_NATIVE_DOUBLE, mem_space, file_space, H5P_DEFAULT, &tmp[0]);
		}
	}

	// clear resources
	H5Dclose(dset);
	H5Dclose(dsetN);
	H5Dclose(dsetF);
	H5Dclose(dsetP);
	H5Sclose(space1d);
	H5Sclose(file_space);
	H5Sclose(mem_space);
	H5Fclose(file);
	return file;
}

//--------------------------------------------------------
// The main code
// The user writes this for their own needs
//--------------------------------------------------------
int main(int argc, char **argv)
{
	//============//
	// INITIALIZE //
	//============//
	// initialize MPI parallelism
	int MPI_myID;
	MPI_Init( &argc, &argv );
	MPI_Comm_rank( MPI_COMM_WORLD, &MPI_myID );

	// open up the lua parameter file
	Lua lua;
	string script_file = string(argv[1]);
	lua.init( script_file );


	testTransport sim;
	sim.init(&lua);
	sim.reset_radiation();

	// read in the output data
	string recover_filename = lua.scalar<string>("RayTracing_recover_file");
	sim.grid->read_zones(recover_filename);
	const int NS = sim.grid->distribution.size();
	const int NE = sim.grid->nu_grid_axis.size();

	// read in starting points
	vector<double> r    = lua.vector<double>("RayTracing_initial_r");
	vector<double> phi  = lua.vector<double>("RayTracing_initial_phi");
	vector<double> z    = lua.vector<double>("RayTracing_initial_z");
	vector<double> kmu  = lua.vector<double>("RayTracing_initial_kmu");
	vector<double> kphi = lua.vector<double>("RayTracing_initial_kphi");
	int ntrajectories = r.size();
	lua.close();


	for(int itraj=0; itraj<ntrajectories; itraj++){
		vector<double> xup(3), kup(3);
		xup[0] = r[itraj] * cos(phi[itraj]*M_PI/180.);
		xup[1] = r[itraj] * sin(phi[itraj]*M_PI/180.);
		xup[2] = z[itraj];
		double rmag = sqrt(r[itraj]*r[itraj] + z[itraj]*z[itraj]);
		double netphi = kphi[itraj]*phi[itraj]*M_PI/180.;
		double sintheta_k = (1.-kmu[itraj]*kmu[itraj]);
		kup[0] = cos(netphi) * sintheta_k;
		kup[1] = sin(netphi) * sintheta_k;
		kup[2] = kmu[itraj];

		// set up filename
		stringstream filename_stream;
		filename_stream << std::fixed;
		filename_stream.precision(1);
		filename_stream << "trajectory_r" << r[itraj]/1e5;
		filename_stream.precision(0);
		filename_stream << "phi" << phi[itraj];
		filename_stream.precision(1);
		filename_stream  << "z"<<z[itraj]/1e5;
		filename_stream << "_kmu"<<kmu[itraj];
		filename_stream.precision(0);
		filename_stream << "kphi"<<kphi[itraj];
		if(DO_GR) filename_stream << "_GR";
		filename_stream << ".h5";
		string outfilename;
		outfilename = filename_stream.str();
		cout << outfilename << endl;
		continue;

		// initialize the EinsteinHelper
		EinsteinHelper eh;
		TrajectoryData td(NS,NE);
		for(int i=0; i<3; i++){
			eh.xup[i] = xup[i];
			eh.kup[i] = kup[i];
		}
		eh.xup[3] = 0;
		eh.s = 0;
		eh.N = 1;
		eh.N0 = eh.N;
		eh.fate = moving;

		sim.update_eh_background(&eh);
		eh.g.normalize_null_changeupt(eh.kup);
		sim.update_eh_k_opac(&eh);
		double ct = 0;
		while(eh.fate==moving){
			append_data(&sim, &eh, ct, &td);
			double d_zone = sim.grid->zone_min_length(eh.z_ind) / sqrt(Metric::dot_Minkowski<3>(eh.kup,eh.kup)) * eh.kup_tet[3];
			eh.ds_com = d_zone * sim.max_step_size;
			ct += eh.ds_com;
			sim.move(&eh, &ct);
		}
		create_file(outfilename, td, sim);
	}

	// exit the program
	MPI_Finalize();
	return 0;
}
