/*
 * main.cc
 *
 *  Created on: 24/mar/2015
 *      Author: nicola
 */

#include "Image.hpp"
#include "Utils.hpp"
#include "DA3D.hpp"
#include "mex.h"

using da3d::Image;
using da3d::DA3D;


Image read_image(const mxArray* im)
{
   int w, h, c;
   mxAssert(mxIsSingle(im), "Input image must be of type single");
   int ndim = mxGetNumberOfDimensions(im);
   const mwSize* dims = mxGetDimensions(im);
   w = dims[0];
   h = dims[1];
   c = (ndim > 2) ? dims[2] : 1;

   float *data = (float*)mxGetData(im);
   return Image(data, h, w, c);
}

mxArray* save_image(const Image& im)
{
   mwSize dims[3] = { im.columns(), im.rows(), im.channels() };
   mxArray* image = mxCreateNumericArray(3, dims, mxSINGLE_CLASS, mxREAL);
   std::copy_n(im.data(), dims[0]*dims[1]*dims[2], (float*)mxGetData(image));
   return image;
}

void mexFunction(int nlhs, mxArray *plhs[],
   int nrhs, const mxArray *prhs[])
{
#ifndef _OPENMP
   cerr << "Warning: OpenMP not available. The algorithm will run in a single" <<
      " thread." << endl;
#endif

   mxAssert(nrhs >= 3, "Needs three input arguments, input, guide and sigma");
   Image input = read_image(prhs[0]);
   Image guide = read_image(prhs[1]);
   float sigma = mxGetScalar(prhs[2]);

   std::vector<float> K_high, K_low;
   Image output = DA3D(input, guide, sigma, K_high, K_low, false);

   if (nlhs > 0)
      plhs[0] = save_image(output);
}