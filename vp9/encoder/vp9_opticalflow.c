/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be
 *  found  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>

#include "vp9/common/vp9_motion_model.h"
#include "vp9/encoder/vp9_opticalflow.h"
#include "vp9/encoder/vp9_ransac.h"
#include "vp9/encoder/vp9_resize.h"
#include "vp9/encoder/eispack.h"
static int seeded = 0;

#define ITERATIVE_ESTIMATE

/*
Top level interface for computing optical flow. This function
takes in two images and returns the pixelwise motion field that
describes the motion between the two images. Note that the warping
needs to be done by SUBTRACTING the flow vectors from the locations
in the second image passed in (ref) in order to produce a prediction
for the first image passed in (frm).
*/
void compute_flow(unsigned char *frm, unsigned char *ref,
                  flowMV *flow, int width, int height,
                  int frm_stride, int ref_stride) {
  flow->u = (double*)malloc(width * height * sizeof(double));
  flow->v = (double*)malloc(width * height * sizeof(double));
  flow->confidence = (double*)malloc(width * height * sizeof(double));

  /* Uses lucas kanade and iterative estimation without coarse to fine.
     This is more efficient for small motion but becomes less accurate
     as motion gets larger. */
  #ifdef ITERATIVE_ESTIMATE
    double *smooth_ref, *smooth_frm;
    double *dx_frm, *dy_frm;

    blur_img(ref, width, height, ref_stride, &smooth_ref, 11, 1.5, 1);
    blur_img(frm, width, height, frm_stride, &smooth_frm, 11, 1.5, 1);
    differentiate((unsigned char*)smooth_frm, sizeof(double), width, height,
                  width, &dx_frm, &dy_frm);
    lucas_kanade_base(smooth_frm, smooth_ref, dx_frm, dy_frm, flow,
                      width, height, width, width);
    iterative_refinement(ref, smooth_frm, dx_frm, dy_frm, flow, width, height,
                         ref_stride, width, 3);
    free(smooth_ref);
    free(smooth_frm);
    free(dx_frm);
    free(dy_frm);

  /* Uses lucas kanade and iterative estimation on an image pyramid to deal
     with temporal aliasing. For small motion, this isn't necessary but it
     dramatically increases performance as motion gets larger. */
  #else
    coarse_to_fine_flow(frm, ref, flow, width, height, frm_stride,
                        ref_stride, 3, 4);
  #endif

  flow->width = width;
  flow->height = height;
}

// computes lucas kanade optical flow. Note that this assumes images that
// already denoised and differentiated.
void lucas_kanade_base(double *smooth_frm, double *smooth_ref,
                       double *dx, double *dy, flowMV *flow,
                       int width, int height, int smooth_frm_stride,
                       int smooth_ref_stride) {
  int i, j;
  int local_neighborhood_sz = 5;
  double window_separable[] = {0.0625, 0.25, 0.375, 0.25, 0.0625};
  double *dt = (double *)malloc(width * height * sizeof(double));
  double *window = (double *)malloc(local_neighborhood_sz *
                                    local_neighborhood_sz * sizeof(double));
  // Create windowing function to properly weight local neighborhood in order
  // to make sure optical flow equation is not underconstrained.
  MultiplyMat(window_separable, window_separable, window,
              local_neighborhood_sz, 1, local_neighborhood_sz);

  // compute temporal derivative
  pointwise_matrix_sub((unsigned char*)smooth_ref, (unsigned char*)smooth_frm,
                       sizeof(double), dt, width, height, smooth_ref_stride,
                       smooth_frm_stride);

  for (i = 0; i < width; ++i)
    for (j = 0; j < height; ++j) {

      optical_flow_per_pixel(dx, dy, dt, window, local_neighborhood_sz,
                             flow, width, height, width, i, j);
    }
  free(dt);
  free(window);
}

// solves lucas kanade equation at any give pixel in the image using a local
// neighborhood for support. loc_x and loc_y are the x and y components
// of the center of the local neighborhood. Assumes dx, dy and dt have same
// stride.
void optical_flow_per_pixel(double *dx, double *dy, double *dt, double *window,
                      int wind_size, flowMV *flow, int width,
                      int height, int stride, int loc_x, int loc_y) {
  int i, j, iw, jw, im_ind;
  double g;
  // M and b are matrices used to solve the equation a = M^-1 * b where a
  // are the desired optical flow parameters
  double M[4] = {0, 0, 0, 0};
  double flow_vec[2];
  double M_inv[4];
  double b[2] = {0, 0};
  // W and Z are empty matrices used to compute eigenvalues
  double W[2] = {0, 0};
  double Z[4] = {0, 0, 0, 0};
  int step = wind_size / 2;

  for (i = loc_x - step; i <= loc_x + step; ++i)
    for (j = loc_y - step; j <= loc_y + step; ++j) {
      // if pixel is out of bounds, use reflected image content
      iw = WRAP_PIXEL(i, width);
      jw = WRAP_PIXEL(j, height);
      im_ind = iw + jw * stride;
      g = window[(i - loc_x + step) + (j - loc_y + step) * wind_size];
      M[0] += g * dx[im_ind] * dx[im_ind];
      M[1] += g * dx[im_ind] * dy[im_ind];
      M[2] += g * dx[im_ind] * dy[im_ind];
      M[3] += g * dy[im_ind] * dy[im_ind];
      b[0] += -g * dx[im_ind] * dt[im_ind];
      b[1] += -g * dy[im_ind] * dt[im_ind];
    }

  PseudoInverse(M_inv, M, 2, 2);
  MultiplyMat(M_inv, b, flow_vec, 2, 2, 1);

  //compute eigen values for confidence metric
  rs(2, M, W, 0, Z);
  flow->confidence[loc_x + loc_y * width] = W[0] < W[1] ?
                                                (1/W[0] > 10 ? 10 : 1/W[0]) :
                                                (1/W[1] > 10 ? 10 : 1/W[1]);
  flow->u[loc_x + loc_y * width] = flow_vec[0];
  flow->v[loc_x + loc_y * width] = flow_vec[1];
}

/* improves an initial approximation for the vector field by iteratively
   warping one image towards the other */
void iterative_refinement(unsigned char *ref, double *smooth_frm, double *dx,
                          double *dy, flowMV *flow, int width,
                          int height, int ref_stride, int smooth_frm_stride,
                          int n_refinements) {
  int i, j, n;
  double x, y;
  unsigned char *estimate = (unsigned char*)malloc(width * height *
                                                   sizeof(unsigned char));
  double *smooth_estimate;
  flowMV new_flow;
  double *new_u = (double*)malloc(width * height * sizeof(double));
  double *new_v = (double*)malloc(width * height * sizeof(double));
  double *new_c = (double*)malloc(width * height * sizeof(double));
  new_flow.u = new_u;
  new_flow.v = new_v;
  new_flow.confidence = new_c;

  // warp one image toward the other
  for (n = 0; n < n_refinements; ++n) {
    for (i = 0; i < width; ++i)
      for (j = 0; j < height; ++j) {
        #ifdef ROTZOOM
          x = (flow->a1[i + j * width] * i) -
              (flow->a2[i + j * width] * j) +
              flow->u[i + j * width];
          y = (flow->a2[i + j * width] * i) +
              (flow->a1[i + j * width] * j) +
              flow->v[i + j * width];

        #else
          x  = i - flow->u[i + j * width];
          y = j - flow->v[i + j * width];
        #endif
        estimate[i + j * width] = interpolate(ref, x, y, width,
                                              height, ref_stride);
      }

    // compute flow between frame and warped estimate
    blur_img(estimate, width, height, width, &smooth_estimate, 11, 1.5, 1);
    lucas_kanade_base(smooth_frm, smooth_estimate, dx, dy, &new_flow,
                      width, height, smooth_frm_stride, width);

    //add residual and apply median filter for denoising
    pointwise_matrix_add((unsigned char*)flow->u, (unsigned char*) new_u,
                         sizeof(double), new_u, width, height, width, width);
    pointwise_matrix_add((unsigned char*) flow->v, (unsigned char*) new_v,
                         sizeof(double), new_v, width, height, width, width);
    median_filter((unsigned char*)new_u, flow->u, sizeof(double), 5, width,
                  height, width);
    median_filter((unsigned char*)new_v, flow->v, sizeof(double), 5, width,
                  height, width);
    median_filter((unsigned char*)new_c, flow->confidence, sizeof(double), 5, width,
                  height, width);
    free(smooth_estimate);
  }

  free(estimate);
  free(new_u);
  free(new_v);
  free(new_c);
}

// computes optical flow on a gaussian pyramid
void coarse_to_fine_flow(unsigned char *frm, unsigned char *ref,
                         flowMV *flow, int width, int height,
                         int frm_stride, int ref_stride, int n_refinements,
                         int n_levels) {
  int i, j, k;
  // w, h, s_r, s_f are intermediate width, height, ref stride, frame stride
  // as the resolution changes up the pyramid
  int w, h, s_r, s_f, w_upscale, h_upscale, s_upscale;
  double x, y;
  int blur_size = 11;
  double blur_sig = 1.5;
  double *smooth_ref, *smooth_frm, *smooth_frm_approx;
  double *dx_frm, *dy_frm, *dx_frm_approx, *dy_frm_approx;
  double *u_level, *v_level, *u_upscale, *v_upscale, *conf_level;
  flowMV flow_level;
  unsigned char *frm_approx;
  imPyramid *frm_pyramid = (imPyramid*)malloc(sizeof(imPyramid));
  imPyramid *ref_pyramid = (imPyramid*)malloc(sizeof(imPyramid));
  // create image pyramids
  image_pyramid(frm, frm_pyramid, width, height, frm_stride, 0.5, n_levels);
  image_pyramid(ref, ref_pyramid, width, height, ref_stride, 0.5, n_levels);
  n_levels = frm_pyramid->n_levels;
  w = frm_pyramid->widths[n_levels - 1];
  h = frm_pyramid->heights[n_levels - 1];
  s_r = ref_pyramid->strides[n_levels - 1];
  s_f = frm_pyramid->strides[n_levels - 1];

  // initialize frame estimation to be the coarsest level of frm_pyramid
  frm_approx = (unsigned char*)malloc(w * h * sizeof(unsigned char));
  memcpy(frm_approx, frm_pyramid->levels[n_levels - 1], w * h *
         sizeof(unsigned char));

  //for each level in the pyramid starting with the coarsest
  for (i = n_levels - 1; i >= 0; --i){

    assert(frm_pyramid->widths[i] == ref_pyramid->widths[i]);
    assert(frm_pyramid->heights[i] == ref_pyramid->heights[i]);
    u_level = (double*)malloc(w * h * sizeof(double));
    v_level = (double*)malloc(w * h * sizeof(double));
    conf_level = (double*)malloc(w * h * sizeof(double));
    flow_level.u = u_level;
    flow_level.v = v_level;
    flow_level.confidence = conf_level;
    //blur and get derivatives
    blur_img(frm_approx, w, h, w, &smooth_frm_approx, blur_size, blur_sig, 1);
    differentiate((unsigned char*)smooth_frm_approx, sizeof(double), w, h, w,
                  &dx_frm_approx, &dy_frm_approx);

    blur_img(ref_pyramid->levels[i], w, h, s_r, &smooth_ref, blur_size,
             blur_sig, 1);

    blur_img(frm_pyramid->levels[i], w, h, s_f, &smooth_frm, blur_size,
             blur_sig, 1);
    differentiate((unsigned char*)smooth_frm, sizeof(double), w, h, w,
                  &dx_frm, &dy_frm);

    // compute optical flow at this level between the reference frame and
    // the estimate produced from warping. If this is the first iteration
    // (meaning no warping has happened yet) then smooth_frm_approx is just
    // the coarsest level of frm_pyramid.
    lucas_kanade_base(smooth_frm_approx, smooth_ref, dx_frm_approx,
                      dy_frm_approx, &flow_level, w, h, w, w);
    if (i < n_levels - 1) {
      pointwise_matrix_add((unsigned char*) u_level,
                           (unsigned char*) u_upscale, sizeof(double),
                           u_upscale, w, h, w, w);
      pointwise_matrix_add((unsigned char*) v_level,
                           (unsigned char*) v_upscale, sizeof(double),
                           v_upscale, w, h, w, w);
      median_filter((unsigned char*)u_upscale, u_level, sizeof(double),
                     5, w, h, w);
      median_filter((unsigned char*)v_upscale, v_level, sizeof(double),
                     5, w, h, w);
      free(u_upscale);
      free(v_upscale);
    }
    iterative_refinement(ref_pyramid->levels[i], smooth_frm, dx_frm, dy_frm,
                         &flow_level, w, h, s_r, w, n_refinements);

    free(smooth_frm);
    free(smooth_ref);
    free(dx_frm);
    free(dy_frm);
    free(frm_approx);
    free(smooth_frm_approx);
    free(dx_frm_approx);
    free(dy_frm_approx);

    //if we're at the finest level, we're ready to return u and v
    if (i == 0) {
      assert(w == width);
      assert(h == height);
      memcpy(flow->u, u_level, w * h * sizeof(double));
      memcpy(flow->v, v_level, w * h * sizeof(double));
      memcpy(flow->confidence, conf_level, w * h * sizeof(double));
      destruct_pyramid(frm_pyramid);
      destruct_pyramid(ref_pyramid);
      free(frm_pyramid);
      free(ref_pyramid);
      free(u_level);
      free(v_level);
      free(conf_level);
      return;
    }

    w_upscale = ref_pyramid->widths[i - 1];
    h_upscale = ref_pyramid->heights[i - 1];
    s_upscale = ref_pyramid->strides[i - 1];
    u_upscale = (double*)malloc(w_upscale * h_upscale * sizeof(double));
    v_upscale = (double*)malloc(w_upscale * h_upscale * sizeof(double));

    frm_approx = (unsigned char*)malloc(w_upscale * h_upscale *
                                        sizeof(unsigned char));
    //warp image according to optical flow estimate
    for (k = 0; k < w_upscale; ++k)
      for (j = 0; j < h_upscale; ++j) {
        u_upscale[k + j * w_upscale] = u_level[(int)(k * 0.5) + (int)(j * 0.5) * w];
        v_upscale[k + j * w_upscale] = v_level[(int)(k * 0.5) + (int)(j * 0.5) * w];
        x = k - u_upscale[k + j * w_upscale];
        y = j - v_upscale[k + j * w_upscale];
        frm_approx[k + j * w_upscale] = interpolate(ref_pyramid->levels[i - 1],
                                        x, y, w_upscale, h_upscale, s_upscale);
      }

    //assign dimensions for next level
    w = frm_pyramid->widths[i - 1];
    h = frm_pyramid->heights[i - 1];
    s_r = ref_pyramid->strides[i - 1];
    s_f = frm_pyramid->strides[i - 1];

    free(u_level);
    free(v_level);
    free(conf_level);
  }
}

/* produces an image pyramid where each level is resized by a
   specified resize factor */
void image_pyramid(unsigned char *img, imPyramid *pyr, int width,
                   int height, int stride, double resize_factor,
                   int n_levels) {
  int i;
  int max_levels = 1;
  int scaled = width < height ? (width/30) : (height/30);
  pyr->widths[0] = width;
  pyr->heights[0] = height;
  pyr->strides[0] = stride;

  if ((RELATIVE_ERROR(resize_factor, 1.0) <= MAX_ERROR) || (n_levels == 1)) {
    pyr->n_levels = 1;
    pyr->levels = (unsigned char**)malloc(sizeof(unsigned char*));
    pyr->levels[0] = img;
    return;
  }
  // compute number of possible levels of the pyramid. The smallest dim of the
  // smallest level should be no less than 30 px.
  while(scaled > 1){
    max_levels++;
    scaled >>= 1;
  }

  if (n_levels > max_levels)
    n_levels = max_levels;

  pyr->n_levels = n_levels;
  pyr->levels = (unsigned char**)malloc(n_levels * sizeof(unsigned char*));
  pyr->levels[0] = img;

  // compute each level
  for (i = 1; i < n_levels; ++i) {
    pyr->widths[i] = pyr->widths[i-1] * resize_factor;
    pyr->heights[i] = pyr->heights[i-1] * resize_factor;
    pyr->strides[i] = pyr->widths[i];
    pyr->levels[i] = (unsigned char*)malloc(pyr->widths[i] *
                                            pyr->heights[i] *
                                            sizeof(unsigned char));

    vp9_resize_plane(pyr->levels[i-1], pyr->heights[i-1], pyr->widths[i-1],
                     pyr->strides[i-1], pyr->levels[i], pyr->heights[i],
                     pyr->widths[i], pyr->strides[i]);
  }
}

void destruct_pyramid(imPyramid *pyr) {
  int i;

  //start at level 1 because level 0 is a reference to the original image
  for(i = 1; i < pyr->n_levels; ++i)
    free(pyr->levels[i]);
  free(pyr->levels);
}

// computes x and y spatial derivatives of an image.
void differentiate(unsigned char *img, size_t elem_size, int width, int height,
                   int stride, double **dx_img, double **dy_img) {
  double spatial_deriv_kernel[] = {-0.0833, 0.6667, 0.0, -0.6667, 0.0833};
  *dx_img = (double *)malloc(width*height*sizeof(double));
  *dy_img = (double *)malloc(width*height*sizeof(double));

  convolve(spatial_deriv_kernel, img, elem_size, *dx_img, 5, 1, width, height,
           stride);
  convolve(spatial_deriv_kernel, img, elem_size, *dy_img, 1, 5, width, height,
           stride);
}

// blurs image with a gaussian kernel
void blur_img(unsigned char *img, int width, int height,
              int stride, double **smooth_img, int smoothing_size,
              double smoothing_sig, double smoothing_amp) {
  double *smoothing_kernel = (double *)malloc(smoothing_size * smoothing_size *
                                              sizeof(double));
  *smooth_img = (double *)malloc(width * height * sizeof(double));
  gaussian_kernel(smoothing_kernel, smoothing_size, smoothing_sig,
                  smoothing_amp);
  convolve(smoothing_kernel, img, sizeof(unsigned char), *smooth_img,
           smoothing_size, smoothing_size, width, height, stride);
  free(smoothing_kernel);
}

// creates a 2D gaussian kernel
void gaussian_kernel(double *mat, int size, double sig, double amp)
{
  int x, y;
  double filter_val;
  size = size%2 == 0 ? size + 1 : size;
  int half = size * 0.5;
  double denominator = 1 / (2.0 * sig * sig);
  double sum = 0.0f;
  for (x = (-1 * half); x<=half; x++)
    for (y = (-1 * half); y<=half; y++)
    {
      filter_val = amp * exp((double)(-1.0 * ((x * x * denominator) +
                   (y * y * denominator))));
      mat[(x + half) + ((y + half) * size)] = filter_val;
      sum += filter_val;
    }

  //normalize the filter
  if (sum > 0.0001)
  {
    sum = 1 / sum;
    for (x = 0; x < size * size; x++)
    {
      mat[x] = mat[x] * sum;
    }
  }
}

// Convolution with reflected content padding
static void convolve(double *filter, unsigned char *image, size_t elem_size,
              double *convolved, int filt_width, int filt_height,
              int image_width, int image_height, int image_stride) {
  int i, j, x, y, imx, imy;
  double pixel_val;
  double filter_val;
  double image_val;
  unsigned char *loc;
  int x_pad_size = filt_width / 2;
  int y_pad_size = filt_height / 2;
  for(i = 0; i < image_width; ++i)
    for (j = 0; j < image_height; ++j)
    {
      pixel_val = 0;
      for(x = (-1 * x_pad_size); x <= x_pad_size; x++)
        for(y = (-1 * y_pad_size); y<= y_pad_size; y++)
        {
          filter_val = filter[(x + x_pad_size) + (y + y_pad_size) * filt_width];
          imx = WRAP_PIXEL(i + x, image_width);
          imy = WRAP_PIXEL(j + y, image_height);
          loc = image + ((imx + imy * image_stride) * elem_size);
          image_val = CHAR_TO_DOUBLE(elem_size, loc);
          pixel_val += (filter_val * image_val);
        }
      convolved[i + j * image_width] =  pixel_val;
    }
}

/* Performs median filtering for denoising. This implementation uses hoare's
   select to find the median in a block. block_x, block_y are
   the x and y coordinates of the center of the block in the source */
void median_filter(unsigned char *source, double *filtered, size_t elem_size,
                    int block_size, int width, int height, int stride) {
  int i, j, block_x, block_y, imx, imy;
  unsigned char *loc;
  double pivot, val;
  int length = block_size * block_size;
  double L[length], E[length], G[length];
  int k = length % 2 == 0 ? length / 2 : (length / 2) + 1;
  int l , e, g;
  int pad_size = block_size / 2;

  if (seeded == 0) {
    srand(time(NULL));
    seeded = 1;
  }

  // find the median within each block. Reflected content is used for padding.
  for (block_x = 0; block_x < width; ++block_x)
    for (block_y = 0; block_y < height; ++block_y) {
      l = 0, e = 0, g = 0;
      memset(L, 0, sizeof(L));
      memset(E, 0, sizeof(E));
      memset(G, 0, sizeof(G));
      loc = source + ((block_x + block_y * stride) * elem_size);
      pivot =  CHAR_TO_DOUBLE(elem_size, loc);
      for (i = -pad_size; i <= pad_size; ++i)
        for (j = -pad_size; j <= pad_size; ++j) {
          imx = WRAP_PIXEL(i + block_x, width);
          imy = WRAP_PIXEL(j + block_y, height);
          loc = source + ((imx + imy * stride) * elem_size);
          val = CHAR_TO_DOUBLE(elem_size, loc);

          // pulling out the first iteration of selection so we don't have
          // to iterate through the block to flatten it and then
          // iterate through those same values to put them into
          // the L E G bins in selection. Ugly but more efficent.
          if (RELATIVE_ERROR(val, pivot) <= MAX_ERROR) {
            E[e] = val;
            e++;
          } else if (val < pivot) {
            L[l] = val;
            l++;
          } else if (val > pivot) {
            G[g] = val;
            g++;
          }
        }
      if (k <= l)
        filtered[block_x + block_y * width] = selection(L, l, k);
      else if (k <= l + e)
        filtered[block_x + block_y * width] = pivot;
      else
        filtered[block_x + block_y * width] = selection(G, g, k - l - e);
  }
}

// Implementation of Hoare's select for linear time median selection.
double selection(double *vals, int length, int k) {
  int pivot_ind, x;
  double pivot;
  double L[length], E[length], G[length];
  int l = 0, e = 0, g = 0;

  if (seeded == 0) {
    srand(time(NULL));
    seeded = 1;
  }
  pivot_ind = (int) ((length / ((double)RAND_MAX)) * rand());
  pivot = vals[pivot_ind];

  for (x = 0; x < length; ++x) {
    if (RELATIVE_ERROR(vals[x], pivot) <= MAX_ERROR) {
      E[e] = vals[x];
      e++;
    } else if (vals[x] < pivot) {
      L[l] = vals[x];
      l++;
    } else if (vals[x] > pivot) {
      G[g] = vals[x];
      g++;
    }
  }

  if (k <= l)
    return selection(L, l, k);
  else if (k <= l + e)
    return pivot;
  else
    return selection(G, g, k - l - e);
}

// Warp an image and print out the mean squared error
inline void pixelwise_warp_mse(unsigned char *frm, unsigned char *ref, flowMV *flow,
                               unsigned char *warped, int width, int height,
                               int ref_stride, int frm_stride) {
  int i, j;
  double x, y, err;
  double mse = 0;
  for (j = 10; j < height-10; ++j){
    for (i = 10; i < width-10; ++i) {
      #ifdef ROTZOOM

        x = (flow->a1[i + j * width] * i) +
            (flow->a2[i + j * width] * j) -
            flow->u[i + j * width];
        y = (-1*flow->a2[i + j * width] * i) +
            (flow->a1[i + j * width] * j) -
            flow->v[i + j * width];
      #else
        x  = i - flow->u[i + j * width];
        y = j - flow->v[i + j * width];
      #endif
      warped[i + j * width] = interpolate(ref, x, y, width, height, ref_stride);
      err = frm[i + j * frm_stride] - warped[i + j * width];
      mse += err * err;
    }
  }

  mse /= ((width-10) * (height-10));
  //printf("%f\n", mse);
}

// Prints a matrix. Mostly for debugging purposes.
static inline void print_mat(unsigned char *mat, size_t size, int width, int height,
                             int stride) {
  int i,j;
  unsigned char *loc;
  double val;

  printf("\n\n");
  for (j = 10; j < height; ++j) {
    for (i = 10; i < width; ++i) {
      loc = mat + ((i + j * stride) * size);
      val = CHAR_TO_DOUBLE(size, loc);
      printf("%f, ", val);
    }
    printf("\n");
  }
  printf("\n\n");
}

// Pointwise matrix operations
void pointwise_matrix_mult(unsigned char *mat1, unsigned char *mat2,
                           size_t elem_size, double *prod, int width,
                           int height, int stride1, int stride2) {
  int i, j;
  unsigned char *loc1, *loc2;
  double val1, val2;
  for (i = 0; i < width; ++i)
    for (j = 0; j < height; ++j){
      loc1 = mat1 + ((i + j * stride1) * elem_size);
      loc2 = mat2 + ((i + j * stride2) * elem_size);
      val1 = CHAR_TO_DOUBLE(elem_size, loc1);
      val2 = CHAR_TO_DOUBLE(elem_size, loc2);
      prod[i + j * width] = val1 * val2;
    }
}

void pointwise_matrix_sub(unsigned char *mat1, unsigned char *mat2,
                           size_t elem_size, double *diff, int width,
                           int height, int stride1, int stride2) {
  int i, j;
  unsigned char *loc1, *loc2;
  double val1, val2;
  for (i = 0; i < width; ++i)
    for (j = 0; j < height; ++j){
      loc1 = mat1 + ((i + j * stride1) * elem_size);
      loc2 = mat2 + ((i + j * stride2) * elem_size);
      val1 = CHAR_TO_DOUBLE(elem_size, loc1);
      val2 = CHAR_TO_DOUBLE(elem_size, loc2);
      diff[i + j * width] = val1 - val2;
    }
}

void pointwise_matrix_add(unsigned char *mat1, unsigned char *mat2,
                           size_t elem_size, double *sum, int width,
                           int height, int stride1, int stride2) {
  int i, j;
  unsigned char *loc1, *loc2;
  double val1, val2;
  for (i = 0; i < width; ++i)
    for (j = 0; j < height; ++j){
      loc1 = mat1 + ((i + j * stride1) * elem_size);
      loc2 = mat2 + ((i + j * stride2) * elem_size);
      val1 = CHAR_TO_DOUBLE(elem_size, loc1);
      val2 = CHAR_TO_DOUBLE(elem_size, loc2);
      sum[i + j * width] = val1 + val2;
    }
}

void pointwise_matrix_div(unsigned char *mat1, unsigned char *mat2,
                           size_t elem_size, double *quot, int width,
                           int height, int stride1, int stride2) {
  int i, j;
  unsigned char *loc1, *loc2;
  double val1, val2;
  for (i = 0; i < width; ++i)
    for (j = 0; j < height; ++j){
      loc1 = mat1 + ((i + j * stride1) * elem_size);
      loc2 = mat2 + ((i + j * stride2) * elem_size);
      val1 = CHAR_TO_DOUBLE(elem_size, loc1);
      val2 = CHAR_TO_DOUBLE(elem_size, loc2);
      quot[i + j * width] = val1 / val2;
    }
}
