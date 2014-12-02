#include <mpi.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <iomanip>
#include <math.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include "Lua.h"
#include "transport.h"
#include "species_general.h"
#include "global_options.h"

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
	int my_rank,n_procs;
	MPI_Init( &argc, &argv );
	MPI_Comm_rank( MPI_COMM_WORLD, &my_rank );
	MPI_Comm_size( MPI_COMM_WORLD, &n_procs);
	const int rank0 = (my_rank == 0);

	// start timer
	double proc_time_start = MPI_Wtime();

	// open up the lua parameter file
	Lua lua;
	string script_file = ( argc>1 ? string(argv[1]) : "param.lua");
	lua.init( script_file );

	// set up the transport module (includes the grid)
	transport sim;
	sim.init(&lua);

	// read in time stepping parameters
	int max_n_steps  = lua.scalar<int>("max_n_steps");
	double dt        = lua.scalar<double>("dt");
	lua.close();

	// initial output
	if(rank0){
		sim.grid->write_zones(0);
		sim.grid->write_rays(0);
	}


	//===========//
	// TIME LOOP //
	//===========//
	if (rank0) printf("%12s %12s %12s %12s\n","iteration","t","dt","n_particles");
	for(int it=1; it<=max_n_steps; it++)
	{
		// do transport step
		sim.step(dt);

		// printout time step
		sim.write(it);
		if(rank0) printf("%12d %12.4e %12.4e %12d\n",it,sim.current_time(),dt, sim.total_particles());
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
	MPI_Finalize();
	return 0;
}
