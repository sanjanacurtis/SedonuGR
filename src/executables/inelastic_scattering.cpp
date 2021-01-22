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
#include "nulib_interface.h"
#include "Species.h"
#include "Grid.h"
#include "global_options.h"

using namespace std;
namespace pc = physical_constants;

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

        class testiScatter : public Transport{
        public:
                void testgrid(){
			//clear global radiation quantities and call set_eas
			reset_radiation();
			
			//reset abs_opac to 1/c for emission
			for(size_t s=0; s<species_list.size(); s++){
                                for(size_t igin=0; igin<15; igin++){
                                        grid->abs_opac[s][igin] = 1./pc::c;	
				}
			}

			//emit from zones per bin (but thermal emission?)
			emit_particles();

			//reset abs_opac to zero since we don't want any absorption
			for(size_t s=0; s<species_list.size(); s++){
                                for(size_t igin=0; igin<15; igin++){
                                        grid->abs_opac[s][igin] = 0;	
				}
			}

			//inelastic scattering loop over emitted particles
			const size_t nparticles = particles.size();
			cout << "Propagate loop..."<<endl;
			for(size_t i=0; i<nparticles; i++){
				cout<<"particle"<<i;
				EinsteinHelper eh;
				eh.set_Particle(particles[i]);
				double tstep = 0.01;
				eh.N0 = eh.N;
				update_eh_background(&eh);
				update_eh_k_opac(&eh);
				// step limit
				while(eh.xup[3]<tstep*pc::c){
					ParticleEvent event;
					which_event(&eh,&event);
					if(event==randomwalk)
                  				random_walk(&eh);
                			else{
                  				move(&eh,false);
                  				if(eh.z_ind>0 and (event==elastic_scatter or event==inelastic_scatter))
                    					scatter(&eh, event);
                			}
				}
				particles[i] = eh.get_Particle();
			}

			//get the distribution
			cout << "Recording distributions..."<< endl;	
			for(size_t i=0; i<nparticles; i++){
				EinsteinHelper eh;
				eh.set_Particle(particles[i]);
				eh.N0 = eh.N;
				update_eh_background(&eh);
				update_eh_k_opac(&eh);
				particles[i] = eh.get_Particle();
				double e = eh.N * eh.kup[3];
                		grid->distribution[eh.s]->count_single(eh.kup_tet, eh.dir_ind, e);
			}
			cout<<"done!";

			//sum and normalize
	        	if(MPI_nprocs>1) sum_to_proc0();
        		normalize_radiative_quantities();
		}
        };

	// set up the transport module (includes the grid and nulib table)
	testiScatter sim;
	sim.init(&lua);
	lua.close();
        // print the fluid properties set in param.lua
        cout << endl << "Currently running: rho=" << sim.grid->rho[0] << "g/ccm T=" << sim.grid->T[0]*pc::k_MeV << "MeV Ye=" << sim.grid->Ye[0] << endl;
	//run the test
	sim.testgrid();
	//write output
	sim.write(1);
	// exit the program	
	MPI_Finalize();
	return 0;
}