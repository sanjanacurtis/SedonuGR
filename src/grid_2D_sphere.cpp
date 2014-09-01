#include <algorithm>
#include <mpi.h>
#include <math.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <cstdio>
#include "H5Cpp.h"
#include "grid_2D_sphere.h"
#include "global_options.h"

//------------------------------------------------------------
// initialize the zone geometry from model file
//------------------------------------------------------------
void grid_2D_sphere::read_model_file(Lua* lua)
{
	// verbocity
	int my_rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &my_rank );
	const int rank0 = (my_rank == 0);

	// generalHDF5 variables
	H5::DataSet dataset;
	H5::DataSpace space;
	H5::CompType comptype;

	// open the model files
	if(rank0) cout << "# Reading the model files..." << endl;
	string model_filename   = lua->scalar<string>("model_file"  );
	string xCoords_filename = lua->scalar<string>("xCoords_file");
	string yCoords_filename = lua->scalar<string>("yCoords_file");
	H5::H5File file(model_filename, H5F_ACC_RDONLY);
	ifstream xCoords_file, yCoords_file;
	xCoords_file.open(xCoords_filename.c_str());
	yCoords_file.open(yCoords_filename.c_str());
	if(xCoords_file.fail()){
		if(rank0) cout << "Error: can't read the xCoords file." << xCoords_filename << endl;
		exit(4);
	}
	if(yCoords_file.fail()){
		if(rank0) cout << "Error: can't read the xCoords file." << yCoords_filename << endl;
		exit(4);
	}

	//====================================================//
	// get the general properties from "/integer scalars" //
	//====================================================//

	// get the database, etc
	dataset = file.openDataSet("/integer scalars");
	space = dataset.getSpace();
	comptype = dataset.getCompType();

	// set up the structures in memory
	const size_t stringsize = 81;
	const int array_length = 15;
	struct pair_t{
		char name[stringsize];
		int value;
	};
	H5::StrType string_type(H5T_STRING,stringsize);
	H5::CompType mempair_t(sizeof(pair_t));
	mempair_t.insertMember("name",HOFFSET(pair_t,name),string_type);
	mempair_t.insertMember("value",HOFFSET(pair_t,value),H5::PredType::STD_I32LE);

	// make sure we have the right dataset
	hsize_t dim;
	const int list_rank = 1;
	assert(space.getSimpleExtentNdims()==list_rank);                 // 1D array
	space.getSimpleExtentDims(&dim);
	assert(dim==array_length);                                       // 15 elements long
	assert(dataset.getTypeClass() == H5T_COMPOUND);                  // filled with structs
	assert(comptype.getNmembers()==2);                               // each struct has 2 elements
	assert(comptype.getMemberName(0)=="name");                       // first element is the name
	assert(comptype.getMemberName(1)=="value");                      // second element is the value
	assert(comptype.getMemberClass(0) == H5T_STRING);                // the type of the first element is a string
	assert(comptype.getMemberClass(1) == H5T_INTEGER);               // the type of the second element is a 32bit little endian int
	assert(comptype.getMemberDataType(0).getSize() == stringsize-1); // the name is 80 characters long, 1 extra for the null terminate

	// read the data
	pair_t integer_data[array_length];
	dataset.read(integer_data,mempair_t);
	for(int i=0; i<array_length; i++) integer_data[i].name[stringsize-1] = '\0';
	assert(trim(string(integer_data[0].name)) == string("nxb")); // make sure we're looking at the right fields
	assert(trim(string(integer_data[1].name)) == string("nyb"));
	assert(trim(string(integer_data[2].name)) == string("nzb"));
	assert(trim(string(integer_data[3].name)) == string("dimensionality"));
	assert(trim(string(integer_data[4].name)) == string("iprocs"));
	assert(trim(string(integer_data[5].name)) == string("jprocs"));
	assert(trim(string(integer_data[6].name)) == string("kprocs"));
	const int nxb = integer_data[0].value;
	const int nyb = integer_data[1].value;
	assert(integer_data[2].value == 1); // 2D dataset should have 1 thickness in the z direction
	assert(integer_data[3].value == 2); // 2D dataset
	const int iprocs = integer_data[4].value;
	const int jprocs = integer_data[5].value;
	assert(integer_data[6].value == 1); // 2D dataset cannot be split in z direction

	// deduce the global structure
	const int nr     = nxb*iprocs;
	const int ntheta = nyb*jprocs;
	const int n_zones = nr*ntheta;
	z.resize(n_zones, zone(3));


	//=========================//
	// read in the actual data //
	//=========================//

	// check that everything makes sense with one of the datasets
	const int dataset_rank = 4;
	hsize_t dims[dataset_rank];
	dataset = file.openDataSet("/dens");
	space = dataset.getSpace();
	space.getSimpleExtentDims(dims);
	assert(dataset.getTypeClass() == H5T_FLOAT);
	assert(space.getSimpleExtentNdims()==dataset_rank);
	assert((int)dims[0] == iprocs*jprocs);
	assert((int)dims[1] == 1);
	assert((int)dims[2] == nyb);
	assert((int)dims[3] == nxb);

	// read the data
	float dens[dims[0]][dims[1]][dims[2]][dims[3]]; // g/ccm
	float velx[dims[0]][dims[1]][dims[2]][dims[3]]; // cm/s
	float vely[dims[0]][dims[1]][dims[2]][dims[3]]; // cm/s
	float angz[dims[0]][dims[1]][dims[2]][dims[3]]; // cm^2/s
	float efrc[dims[0]][dims[1]][dims[2]][dims[3]]; //
	float temp[dims[0]][dims[1]][dims[2]][dims[3]]; // K
	float hvis[dims[0]][dims[1]][dims[2]][dims[3]]; // erg/g/s
	dataset = file.openDataSet("/dens");
	dataset.read(&(dens[0][0][0][0]),H5::PredType::IEEE_F32LE);
	dataset = file.openDataSet("/velx");
	dataset.read(&(velx[0][0][0][0]),H5::PredType::IEEE_F32LE);
	dataset = file.openDataSet("/vely");
	dataset.read(&(vely[0][0][0][0]),H5::PredType::IEEE_F32LE);
	dataset = file.openDataSet("/angz");
	dataset.read(&(angz[0][0][0][0]),H5::PredType::IEEE_F32LE);
	dataset = file.openDataSet("/efrc");
	dataset.read(&(efrc[0][0][0][0]),H5::PredType::IEEE_F32LE);
	dataset = file.openDataSet("/temp");
	dataset.read(&(temp[0][0][0][0]),H5::PredType::IEEE_F32LE);
	dataset = file.openDataSet("/hvis");
	dataset.read(&(hvis[0][0][0][0]),H5::PredType::IEEE_F32LE);
	dataset.close();
	file.close();


	//=========================//
	// read in the coordinates //
	//=========================//

	// check that the files have the correct number of lines
	const int nghost = 4;
	int x_linecount=0, y_linecount=0;
	string line;
	float trash = 0;
	while(getline(xCoords_file,line)) x_linecount++;
	while(getline(yCoords_file,line)) y_linecount++;
	assert(x_linecount == iprocs*nxb+2*nghost);
	assert(y_linecount == jprocs*nyb+2*nghost);

	// read x (r) coordinates
	r_out.resize(nr);
	xCoords_file.clear();                                   // clear bad state from EOF
	xCoords_file.seekg(0);                                  // seek back to beginning of the file
	for(int i=0; i<nghost; i++) getline(xCoords_file,line); // trash the first four lines (ghost points)
	for(int i=0; i<nr; i++){
		if(i==0) xCoords_file >> r_out.min;
		else xCoords_file >> trash;
		xCoords_file >> trash;
		xCoords_file >> r_out[i];
		xCoords_file >> trash;
	}

	// read the y (theta) coordinates
	theta_out.resize(ntheta);
	yCoords_file.clear();  // clear bad state from EOF
	yCoords_file.seekg(0); // seek back to beginning of the file
	for(int i=0; i<nghost; i++) getline(yCoords_file,line); // trash the first four lines (ghost points)
	for(int i=0; i<ntheta; i++){
		if(i==0) yCoords_file >> theta_out.min;
		else yCoords_file >> trash;
		yCoords_file >> trash;
		yCoords_file >> theta_out[i];
		yCoords_file >> trash;
	}


	//===============//
	// fill the grid //
	//===============//
	int do_visc = lua->scalar<int>("do_visc");
	const int kb = 0;
#pragma omp parallel for
	for(unsigned proc=0; proc<dims[0]; proc++){
		for(unsigned jb=0; jb<dims[2]; jb++) for(unsigned ib=0; ib<dims[3]; ib++){
			// indices. moving by one proc in the x direction increases proc by 1
			const int i_global = (proc%iprocs)*nxb + ib;
			const int j_global = (proc/iprocs)*nyb + jb;
			const int z_ind = zone_index(i_global, j_global);
			assert(i_global < nr);
			assert(j_global < ntheta);
			assert(z_ind < n_zones);

			// zone position
			vector<double> r;
			zone_coordinates(z_ind,r);
			assert(r.size()==2);

			// zone values
			z[z_ind].rho           = dens[proc][kb][jb][ib];
			z[z_ind].T_gas         = temp[proc][kb][jb][ib];
			z[z_ind].Ye            = efrc[proc][kb][jb][ib];
			if(do_visc) z[z_ind].H = hvis[proc][kb][jb][ib];
			double vr              = velx[proc][kb][jb][ib];
			double vtheta          = vely[proc][kb][jb][ib];
			double vphi            = angz[proc][kb][jb][ib]/r[0];
			double speed2 = vr*vr + vtheta*vtheta + vphi*vphi;
			if(speed2 >= pc::c*pc::c){
				vr     *= (1.0-tiny)* pc::c*pc::c/speed2;
				vtheta *= (1.0-tiny)* pc::c*pc::c/speed2;
				vphi   *= (1.0-tiny)* pc::c*pc::c/speed2;
			}
			assert(fabs(vr) < pc::c);
			assert(fabs(vtheta) < pc::c);
			assert(vr*vr + vtheta*vtheta < pc::c*pc::c);
			assert((int)z[z_ind].v.size()==3);
			z[z_ind].v[0] = vr;
			z[z_ind].v[1] = vtheta;
			z[z_ind].v[2] = vphi;
			assert(z[z_ind].rho   >= 0.0);
			assert(z[z_ind].T_gas >= 0.0);
			assert(z[z_ind].Ye    >= 0.0);
			assert(z[z_ind].Ye    <= 1.0);
		}
	}
}

//------------------------------------------------------------
// Write a custom model here if you like
//------------------------------------------------------------
void grid_2D_sphere::custom_model(Lua* lua)
{
	// verbocity
	int my_rank;
	MPI_Comm_rank( MPI_COMM_WORLD, &my_rank );
	const int rank0 = (my_rank == 0);
	if(rank0) cout << "#   Reading 1D model file, mapping to 2D" << endl;

	// open up the model file, complaining if it fails to open
	string model_file = "neutron_star.mod";
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
	int r_zones,theta_zones;
	theta_zones = 4;
	infile >> r_zones;
	assert(r_zones > 0);
	z.resize(r_zones*theta_zones,zone(3));
	r_out.resize(r_zones);
	theta_out.resize(theta_zones);

	// read zone properties
	infile >> r_out.min;
	assert(r_out.min >= 0);
	theta_out.min = 0;
	for(int j=0; j<theta_zones; j++) theta_out[j] = theta_out.min + (double)(j+1) * pc::pi / (double)theta_zones;
	for(int i=0; i<r_zones; i++)
	{
		infile >> r_out[i];
		assert(r_out[i] > (i==0 ? r_out.min : r_out[i-1]));

		int base_ind = zone_index(i,0);
		infile >> z[base_ind].rho;
		infile >> z[base_ind].T_gas;
		infile >> z[base_ind].Ye;
		z[base_ind].H = 0;
		z[base_ind].e_rad = 0;
		assert(z[base_ind].v.size() == 3);
		z[base_ind].v[0] = 0;
		z[base_ind].v[1] = 0;
		assert(z[base_ind].rho >= 0);
		assert(z[base_ind].T_gas >= 0);
		assert(z[base_ind].Ye >= 0);
		assert(z[base_ind].Ye <= 1.0);

		for(int j=0; j<theta_zones; j++){
			int z_ind = zone_index(i,j);
			z[z_ind] = z[base_ind];
		}
	}

	infile.close();
}

//------------------------------------------------------------
// Return the zone index containing the position x
//------------------------------------------------------------
int grid_2D_sphere::zone_index(const vector<double>& x) const
{
	assert(x.size()==3);
	const double r  = sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
	const double theta = atan2(sqrt(x[0]*x[0] + x[1]*x[1]), x[2]);
	assert(r >= 0);
	assert(theta >= 0);
	assert(theta <= pc::pi);

	// check if off the boundaries
	if(r     <= r_out.min                    ) return -1;
	if(r     >  r_out[r_out.size()-1]        ) return -2;
	if(theta <= theta_out.min                ) return -2;
	if(theta >  theta_out[theta_out.size()-1]) return -2;

	// find in zone array using stl algorithm upper_bound and subtracting iterators
	const int i =     r_out.locate(r    );
	const int j = theta_out.locate(theta);
	const int z_ind = zone_index(i,j);
	assert(z_ind >= 0);
	assert(z_ind < (int)z.size());
	return z_ind;
}

//----------------------------------------------------------------
// Return the zone index corresponding to the directional indices
//----------------------------------------------------------------
int grid_2D_sphere::zone_index(const int i, const int j) const
{
	assert(i >= 0);
	assert(j >= 0);
	assert(i < (int)r_out.size());
	assert(j < (int)theta_out.size());
	const int z_ind = i*theta_out.size() + j;
	assert(z_ind < (int)z.size());
	return z_ind;
}


//------------------------------------
// get the velocity squared of a zone
//------------------------------------
double grid_2D_sphere::zone_speed2(const int z_ind) const{
	assert(z_ind >= 0);
	assert(z_ind < (int)z.size());
	vector<double> r;
	zone_coordinates(z_ind,r);
	assert((int)r.size()==2);
	const vector<double> v = z[z_ind].v;
	double speed2 = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
	assert(speed2 <= pc::c*pc::c);
	return speed2;
}


//------------------------------------------------------------
// return volume of zone
//------------------------------------------------------------
double grid_2D_sphere::zone_volume(const int z_ind) const
{
	assert(z_ind >= 0);
	assert(z_ind < (int)z.size());
	vector<int> dir_ind;
	zone_directional_indices(z_ind,dir_ind);
	assert(dir_ind.size()==2);
	const unsigned i = dir_ind[0];
	const unsigned j = dir_ind[1];
	const double r0     =     r_out.bottom(i);
	const double theta0 = theta_out.bottom(j);
	const double r1     =     r_out[i];
	const double theta1 = theta_out[j];
	const double vol = 2.0*pc::pi/3.0 * (cos(theta0) - cos(theta1)) * (r1*r1*r1 - r0*r0*r0);
	assert(vol >= 0);
	return vol;
}


//------------------------------------------------------------
// return length of zone
//------------------------------------------------------------
double grid_2D_sphere::zone_min_length(const int z_ind) const
{
	vector<int> dir_ind;
	zone_directional_indices(z_ind,dir_ind);
	assert((int)dir_ind.size()==2);
	const unsigned i = dir_ind[0];
	const unsigned j = dir_ind[1];

	// the 'minimum lengts' are just approximate.
	const double r_len     = (    r_out[i] -     r_out.bottom(i));
	const double theta_len = (theta_out[j] - theta_out.bottom(j)) * r_out.bottom(i);

	// if r_in is zero, there will be problems, but simulations would not have done this.
	if(r_out.bottom(i) == 0) return r_len;
	else return min(r_len, theta_len);

}

//------------------------------------------------------------
// Return the cell-center spherical coordinates of the cell
//------------------------------------------------------------
void grid_2D_sphere::zone_coordinates(const int z_ind, vector<double>& r) const
{
	assert(z_ind >= 0);
	assert(z_ind < (int)(r_out.size()*theta_out.size()));
	r.resize(2);

	vector<int> dir_ind(2,0);
	zone_directional_indices(z_ind, dir_ind);
	assert(dir_ind.size() == 2);
	const unsigned i = dir_ind[0];
	const unsigned j = dir_ind[1];

	const double r0     =     r_out.bottom(i);
	const double theta0 = theta_out.bottom(j);
	r[0] = 0.5 * (r0     +     r_out[i]);
	r[1] = 0.5 * (theta0 + theta_out[j]);
}


//-------------------------------------------
// get directional indices from zone index
//-------------------------------------------
void grid_2D_sphere::zone_directional_indices(const int z_ind, vector<int>& dir_ind) const
{
	assert(z_ind >= 0);
	assert(z_ind < (int)z.size());
	dir_ind.resize(2);
	dir_ind[0] = z_ind / theta_out.size();
	dir_ind[1] = z_ind % theta_out.size();
	assert(dir_ind[0] >= 0);
	assert(dir_ind[1] >= 0);
	assert(dir_ind[0] < (int)    r_out.size());
	assert(dir_ind[1] < (int)theta_out.size());
}


//------------------------------------------------------------
// sample a random cartesian position within the spherical shell
//------------------------------------------------------------
void grid_2D_sphere::cartesian_sample_in_zone(const int z_ind, const vector<double>& rand, vector<double>& x) const
{
	assert(z_ind >= 0);
	assert(z_ind < (int)z.size());
	assert(rand.size()==3);
	x.resize(3);

	// radius and theta indices
	vector<int> dir_ind;
	zone_directional_indices(z_ind,dir_ind);
	int i = dir_ind[0];
	int j = dir_ind[1];

	// inner and outer coordinates of shell
	double  r0 =         r_out.bottom(i);
	double mu0 = cos(theta_out.bottom(j));
	double  r1 =         r_out[i];
	double mu1 = cos(theta_out[j]);

	// sample radial position in shell using a probability integral transform
	double radius = pow( rand[0]*(r1*r1*r1 - r0*r0*r0) + r0*r0*r0, 1./3.);

	// sample cos(theta) uniformily
	double mu = mu0 + (mu1-mu0)*rand[1];
	double sin_theta = sqrt(1-mu*mu);

	// sample phi uniformily
	double phi = 2.0*pc::pi*rand[2];

	// set the real 3-d coordinates. remember, z is along the symmetry axis
	x[0] = radius*sin_theta*cos(phi);
	x[1] = radius*sin_theta*sin(phi);
	x[2] = radius*mu;
}


//------------------------------------------------------------
// get the cartesian velocity vector (cm/s)
//------------------------------------------------------------
void grid_2D_sphere::cartesian_velocity_vector(const vector<double>& x, vector<double>& v) const
{
	assert(x.size()==3);
	v.resize(3,0);
	int z_ind = zone_index(x);
	assert(z_ind >= -1);

	// if within inner sphere, z_ind=-1. Leave velocity at 0.
	if(z_ind >= 0){

		// radius in zone
		double r    = sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
		double rhat = sqrt(x[0]*x[0] + x[1]*x[1]);
		int along_axis = (rhat/r < tiny);

		// Based on position, calculate what the 3-velocity is
		assert(z[z_ind].v.size()==3);
		double vr     = z[z_ind].v[0];
		double vtheta = z[z_ind].v[1];
		double vphi   = z[z_ind].v[2];

		vector<double> vr_cart(3,0);
		vr_cart[0] = vr * x[0]/r;
		vr_cart[1] = vr * x[1]/r;
		vr_cart[2] = vr * x[2]/r;

		vector<double> vtheta_cart(3,0);
		vtheta_cart[0] =  (along_axis ? 0 : vtheta * x[2]/r * x[0]/rhat );
		vtheta_cart[1] =  (along_axis ? 0 : vtheta * x[2]/r * x[1]/rhat );
		vtheta_cart[2] = -vtheta * rhat/r;

		vector<double> vphi_cart(3,0);
		vphi_cart[0] = (along_axis ? 0 : -vphi * x[1]/rhat );
		vphi_cart[1] = (along_axis ? 0 :  vphi * x[0]/rhat );
		vphi_cart[2] = 0;

		// remember, symmetry axis is along the z-axis
		for(int i=0; i<3; i++) v[i] = vr_cart[i] + vtheta_cart[i] + vphi_cart[i];

		// check for pathological case
		if (r == 0){ // set everything to 0
			v[0] = 0;
			v[1] = 0;
			v[2] = 0;
		}
	}

	assert(v[0]*v[0] + v[1]*v[1] + v[2]*v[2] <= pc::c*pc::c);
}


//------------------------------------------------------------
// Write the grid information out to a file
//------------------------------------------------------------
void grid_2D_sphere::write_rays(int iw) const
{
	assert(iw >= 0);
	ofstream outf;
	unsigned i=0,j=0;
	vector<double> r;

	// along theta=0
	transport::open_file("ray_t0",iw,outf);
	zone::write_header(2,outf);
	j = 0;
	for(i=0; i<r_out.size(); i++){
		int z_ind = zone_index(i,j);
		zone_coordinates(z_ind,r);
		z[z_ind].write_line(r,outf);
	}
	outf.close();

	// along theta=pi/2
	transport::open_file("ray_t.5",iw,outf);
	zone::write_header(2,outf);
	j = theta_out.size()/2;
	for(i=0; i<r_out.size(); i++){
		int z_ind = zone_index(i,j);
		zone_coordinates(z_ind,r);
		z[z_ind].write_line(r,outf);
	}
	outf.close();

	// along theta=pi
	transport::open_file("ray_t1",iw,outf);
	zone::write_header(2,outf);
	j = theta_out.size()-1;
	for(i=0; i<r_out.size(); i++){
		int z_ind = zone_index(i,j);
		zone_coordinates(z_ind,r);
		z[z_ind].write_line(r,outf);
	}
	outf.close();

	// along theta
	transport::open_file("ray_r.5",iw,outf);
	zone::write_header(2,outf);
	i = r_out.size()/2;
	for(j=0; j<theta_out.size(); j++){
		int z_ind = zone_index(i,j);
		zone_coordinates(z_ind,r);
		z[z_ind].write_line(r,outf);
	}
	outf.close();
}


//------------------------------------------------------------
// Reflect off the outer boundary
//------------------------------------------------------------
void grid_2D_sphere::reflect_outer(particle *p) const{
	assert(r_out.size()>=1);
	double r0 = (r_out.size()==1 ? r_out.min : r_out.size()-2);
	double dr = r_out.size()-1 - r0;
	assert( fabs(p->r() - r_out[r_out.size()-1]) < tiny*dr);  double velDotRhat = p->mu();

	// invert the radial component of the velocity
	for(int i=0; i<3; i++) p->D[i] -= 2.*velDotRhat * p->x[i]/p->r();
	p->normalize_direction();

	// put the particle just inside the boundary
	double newR = r_out[r_out.size()-1] - tiny*dr;
	for(int i=0; i<3; i++) p->x[i] = p->x[i]/p->r()*newR;

	// must be inside the boundary, or will get flagged as escaped
	assert(zone_index(p->x) >= 0);
}


//------------------------------------------------------------
// Find distance to outer boundary
//------------------------------------------------------------
double grid_2D_sphere::dist_to_boundary(const particle *p) const{
	// Theta = angle between radius vector and direction (Pi if outgoing)
	// Phi   = Pi - Theta (angle on the triangle) (0 if outgoing)
	double Rout  = r_out[r_out.size()-1];
	double Rin   = r_out.min;
	double r  = p->r();
	double mu = p->mu();
	double d_outer_boundary = numeric_limits<double>::infinity();
	double d_inner_boundary = numeric_limits<double>::infinity();
	assert(r<Rout);
	assert(zone_index(p->x) >= -1);

	// distance to inner boundary
	if(r >= Rin){
		double radical = r*r*(mu*mu-1.0) + Rin*Rin;
		if(Rin>0 && mu<0 && radical>=0){
			d_inner_boundary = -r*mu - sqrt(radical);
			assert(d_inner_boundary <= sqrt(Rout*Rout-Rin*Rin)*(1.0+tiny));
		}
	}
	else{
		d_inner_boundary = -r*mu + sqrt(r*r*(mu*mu-1.0) + Rin*Rin);
		assert(d_inner_boundary <= 2.*Rin);
	}
	assert(d_inner_boundary >= 0);

	// distance to outer boundary
	d_outer_boundary = -r*mu + sqrt(r*r*(mu*mu-1.0) + Rout*Rout);
	assert(d_outer_boundary >= 0);
	assert(d_outer_boundary <= 2.*Rout);

	// distances to the theta boundaries - NOT IMPLEMENTED THETA BOUNDARIES
	assert( fabs((theta_out[theta_out.size()-1] - theta_out.min) - pc::pi) < tiny);
	double theta_dist = INFINITY;

	// make sure the particle ends up in a reasonable place
	const double r_dist = min(d_inner_boundary, d_outer_boundary);
	return min(r_dist,theta_dist);
}
