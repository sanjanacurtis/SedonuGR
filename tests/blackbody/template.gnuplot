set pointsize 0.25

set log x
set log y
set title "Varying Density"
set xlabel "Density (g/ccm)"
set ylabel "BB Energy Density (erg/ccm)"
set term pdf
set output "rho.pdf"
plot 'predicted.dat' u 2:((abs($3-TEMP_HERE)/$3<1e-4)&&(abs($4-YE_HERE)/$4<1e-4)?$1:1/0) w l title "Predicted", \
     'results.dat'   u 1:((abs($2-TEMP_HERE)/$2<1e-4)&&(abs($3-YE_HERE)/$3<1e-4)?$5+$6+$7:1/0)     title "Sedonu"

set log x
set log y
set title "Varying Temperature"
set xlabel "Temperature (MeV)"
set ylabel "BB Energy Density (erg/ccm)"
set term pdf
set output "temp.pdf"
plot 'predicted.dat' u 3:((abs($2-RHO_HERE)/$2<1e-4)&&(abs($4-YE_HERE)/$4<1e-4)?$1:1/0) w l title "Predicted", \
     'results.dat'   u 2:((abs($1-RHO_HERE)/$1<1e-4)&&(abs($3-YE_HERE)/$3<1e-4)?$5+$6+$7:1/0)     title "Sedonu"

unset log x
set log y
set title "Varying Electron Fraction"
set xlabel "Ye"
set ylabel "BB Energy Density (erg/ccm)"
set term pdf
set output "ye.pdf"
plot 'predicted.dat' u 4:((abs($3-TEMP_HERE)/$3<1e-4)&&(abs($2-RHO_HERE)/$2<1e-4)?$1:1/0) w l title "Predicted", \
     'results.dat'   u 3:((abs($2-TEMP_HERE)/$2<1e-4)&&(abs($1-RHO_HERE)/$1<1e-4)?$5+$6+$7:1/0)     title "Sedonu"
     