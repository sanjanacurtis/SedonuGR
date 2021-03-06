-- Included Physics

do_randomwalk = 1
do_annihilation = 0

-- Opacity and Emissivity

neutrino_type = "NuLib"
nulib_table = "../../external/NuLib/NuLib.h5"
nulib_eos = "../../external/Hempel_SFHoEOS_rho222_temp180_ye60_version_1.1_20120817.h5"

-- Escape Spectra

spec_n_mu       = 1
spec_n_phi      = 1

-- Distribution Function

distribution_type = "Polar"
distribution_nmu = 2
distribution_nphi = 2

-- Grid and Model

grid_type = "Grid0DIsotropic"
Grid0DIsotropic_rho = 0
Grid0DIsotropic_T   = 0
Grid0DIsotropic_Ye  = 0

-- Output

write_zones_every   = 1

-- Particle Creation

n_subcycles = 1
n_emit_core_per_bin    = 0
n_emit_therm_per_bin   = 50

-- Inner Source

r_core = 0

-- General Controls

verbose       = 1
min_step_size = 0.01
max_step_size = 0.4

-- Biasing

min_packet_weight = 0.01

-- Random Walk

randomwalk_max_x = 2
randomwalk_sumN = 1000
randomwalk_npoints = 200
randomwalk_min_optical_depth = 100
