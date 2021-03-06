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
#include "LuaRead.h"
#include "Transport.h"
#include "Grid.h"
#include "nulib_interface.h"
#include "global_options.h"

using namespace std;
namespace pc = physical_constants;

void run_test(const bool rank0, const double rho, const double T, const double ye, Transport& sim, ofstream& outf){
	if(rank0) cout << endl << "Currently running: rho=" << rho << "g/ccm T=" << T << "MeV Ye=" << ye << endl;

	// set the fluid properties
	sim.grid->rho[0] = rho;
	sim.grid->T[0]   = T/pc::k_MeV;
	sim.grid->Ye[0]  = ye;

	// do the transport step
	sim.step();

	// get the chemical potential
	double munue = nulib_eos_munue(rho,T/pc::k_MeV,ye);
	if(rank0){
		outf << rho << "\t";
		outf << T << "\t";
		outf << ye << "\t";
		outf << munue*pc::ergs_to_MeV << "\t";
		for(size_t i=0; i<sim.species_list.size(); i++)
			outf << sim.grid->distribution[i]->total() << "\t";
		outf << endl;
	}

	// write the data out to file
	//if(rank0) sim.grid->write_line(outf,0);
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
	const int rank0 = (MPI_myID == 0);

	// read command line input
	double max_logrho, min_logrho, rho0;
	double max_logT  , min_logT  , T0;
	double max_ye    , min_ye    , ye0;
	int n_rho, n_T, n_ye;
	PRINT_ASSERT(argc,==,15);
	sscanf(argv[2 ], "%lf", &min_logrho);
	sscanf(argv[3 ], "%lf", &max_logrho);
	sscanf(argv[4 ], "%lf", &rho0);
	sscanf(argv[5 ], "%d" , &n_rho);
	sscanf(argv[6 ], "%lf", &min_logT);
	sscanf(argv[7 ], "%lf", &max_logT);
	sscanf(argv[8 ], "%lf", &T0);
	sscanf(argv[9 ], "%d" , &n_T);
	sscanf(argv[10], "%lf", &min_ye);
	sscanf(argv[11], "%lf", &max_ye);
	sscanf(argv[12], "%lf", &ye0);
	sscanf(argv[13], "%d" , &n_ye);
	double dlogT   = (max_logT   - min_logT  ) / ((double)n_T   - 1.0);
	double dlogrho = (max_logrho - min_logrho) / ((double)n_rho - 1.0);
	double dye     = (max_ye     - min_ye    ) / ((double)n_ye  - 1.0);

	// start timer
	double proc_time_start = MPI_Wtime();

	// read in eos table
	//nulib_eos_read_table(argv[14]);

	// open up the lua parameter file
	Lua lua;
	string script_file = string(argv[1]);
	lua.init( script_file );

	// set up the transport module (includes the grid)
	Transport sim;
	sim.init(&lua);
	lua.close();

	// check parameters
	PRINT_ASSERT(sim.r_core,==,0);

	// open the output file
	ofstream outf;
	if(rank0){
		outf.open("results.dat");
	}

	//==============//
	// DENSITY LOOP //
	//==============//
	for(int i=0; i<n_rho; i++){
		double logrho = min_logrho + i*dlogrho;
		run_test(rank0,pow(10,logrho),T0,ye0,sim,outf);
	}
	//==================//
	// TEMPERATURE LOOP //
	//==================//
	for(int i=0; i<n_T; i++){
		double logT = min_logT + i*dlogT;
		run_test(rank0,rho0,pow(10,logT),ye0,sim,outf);
	}
	//=========//
	// YE LOOP //
	//=========//
	for(int i=0; i<n_ye; i++){
		double ye = min_ye + i*dye;
		run_test(rank0,rho0,T0,ye,sim,outf);
	}

	//===================//
	// FINALIZE AND EXIT //
	//===================//
	// calculate the elapsed time
	double proc_time_end = MPI_Wtime();
	double time_wasted = proc_time_end - proc_time_start;
	if (rank0) printf("#\n# CALCULATION took %.3e seconds or %.3f mins or %.3f hours\n",
			time_wasted,time_wasted/60.0,time_wasted/60.0/60.0);

	// exit the program
	if(rank0) outf.close();
	MPI_Finalize();
	return 0;
}
