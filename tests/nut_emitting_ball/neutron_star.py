MeV = 1.1605e10 # Kelvin
km = 1.0e5      # cm

nx = 1
t0 = -1

R_max = 1*km
R_min = 0*km
dx = (R_max-R_min)/nx

rho = 1e10
temp = 5*MeV
ye = 0.3

print '1D_sphere', 'GRB',nx,R_min,t0

for i  in range(1,nx+1):
    R = R_min + i*dx
    print R, rho, temp, ye
