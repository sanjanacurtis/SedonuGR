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
#include "Neutrino_GR1D.h"
#include "Transport.h"
#include "Grid.h"
#include "nulib_interface.h"
#include "H5Cpp.h"

using namespace std;
namespace pc = physical_constants;


// constructor
Neutrino_GR1D::Neutrino_GR1D(){
	ghosts1 = -1;
	n_GR1D_zones = -1;
}

//----------------------------------------------------------------
// called from species_general::init (neutrino-specific stuff)
//----------------------------------------------------------------
void Neutrino_GR1D::myInit(Lua* lua)
{
	// set neutrino's min and max values
	T_min  =  0;
	T_max  =  INFINITY;
	Ye_min =  0;
	Ye_max =  1.0;
	rho_min = 0;
	rho_max = INFINITY;

	// get nugrid from nulib file
	string nulibfilename = lua->scalar<string>("nulib_file");
	H5::H5File file( nulibfilename.c_str(), H5F_ACC_RDONLY );
	H5::DataSet dataset = file.openDataSet("bin_top");
	H5::DataSpace dataspace = dataset.getSpace();
	PRINT_ASSERT(dataspace.getSimpleExtentNdims(),==,1);
	hsize_t dims[1];
	dataspace.getSimpleExtentDims(dims);
	nu_grid.resize(dims[0]);
	dataset.read(&(nu_grid.x[0]),H5::PredType::IEEE_F64LE);
	dataset.close();
	file.close();
	nu_grid.min = 0;
	for(int i=0; i<nu_grid.size(); i++)	nu_grid.x[i] /= pc::h_MeV;

	// let the base class do the rest
	Neutrino::myInit(lua);
}

//-----------------------------------------------------------------
// set emissivity, abs. opacity, and scat. opacity in zones
//-----------------------------------------------------------------
void Neutrino_GR1D::set_eas(int zone_index)
{
	// do nothing - opacities are set externally
}


void Neutrino_GR1D::set_eas_external(const double* easarray){
	PRINT_ASSERT(ghosts1,>=,0);
	PRINT_ASSERT(n_GR1D_zones,>=,emis.size());
	const int ngroups=emis[0].size();
	const int nspecies = sim->species_list.size();

	for(int z_ind=0; z_ind<emis.size(); z_ind++){
		for(int inu=0; inu<ngroups; inu++){
			// indexed as eas(zone,species,group,e/a/s). The leftmost one varies fastest.
			int eind = (z_ind+ghosts1) + ID*n_GR1D_zones + inu*nspecies*n_GR1D_zones + 0*ngroups*nspecies*n_GR1D_zones;
			int aind = (z_ind+ghosts1) + ID*n_GR1D_zones + inu*nspecies*n_GR1D_zones + 1*ngroups*nspecies*n_GR1D_zones;
			int sind = (z_ind+ghosts1) + ID*n_GR1D_zones + inu*nspecies*n_GR1D_zones + 2*ngroups*nspecies*n_GR1D_zones;

			PRINT_ASSERT(easarray[eind],>=,0);
			PRINT_ASSERT(easarray[aind],>=,0);
			PRINT_ASSERT(easarray[sind],>=,0);

			emis[z_ind].set_value(inu,easarray[eind] / nulib_emissivity_gf);
			abs_opac[z_ind][inu] = easarray[aind] / nulib_opacity_gf;
			scat_opac[z_ind][inu] = easarray[sind] / nulib_opacity_gf;
			biased_emis[z_ind].set_value(inu,emis[z_ind].get_value(inu));
		}
		emis[z_ind].normalize(0);
	}
}


