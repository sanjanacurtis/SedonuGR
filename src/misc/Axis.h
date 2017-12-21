#ifndef _AXIS_H
#define _AXIS_H 1

#include <vector>
#include <algorithm>
#include "global_options.h"

using namespace std;

//======//
// AXIS //
//======//
class Axis{
public:
	double min;
	vector<double> top;
	vector<double> mid;

	Axis(const double min, vector<double>& top, vector<double>& mid){
		assert(top.size() == mid.size());
		this->min = min;
		this->top = top;
		this->mid = mid;
		for(int i=0; i<top.size(); i++){
			PRINT_ASSERT(top[i],>,mid[i]);
			PRINT_ASSERT(mid[i],>, i==0 ? min : top[i-1]);
		}
	}
	Axis(const double min, const double max, const unsigned nbins){
		this->min = min;
		top.resize(nbins);
		mid.resize(nbins);
		double del = (max-min) / (double)nbins;
		for(int i=0; i<nbins; i++){
			top[i] = min + (i+1)*del;
			mid[i] = min + ((double)i + 0.5)*del;
		}
	}

	Axis() {
		min = NaN;
	}

	int size() const {return top.size();}

	int bin(const double x) const{
		if(x<min) return -1;
		else{
			// upper_bound returns first element greater than xval
			// values mark bin tops, so this is what we want
			int ind = upper_bound(top.begin(), top.end(), x) - top.begin();
			assert(ind>=0);
			assert(ind<=(int)top.size());
			return ind;
		}
	}

	double bottom(const int i) const{
		return i==0 ? min : top[i-1];
	}

	double delta(const int i) const{
		return top[i] - bottom(i);
	}
	double delta3(const int i) const{
		return top[i]*top[i]*top[i] - bottom(i)*bottom(i)*bottom(i);
	}
	double max() const{
		return top[size()-1];
	}
};

#endif
