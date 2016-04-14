#include "Image.hpp"
#include "DA3D.hpp"
#include "WeightMap.hpp"
#include "Utils.hpp"
#include "DftPatch.hpp"
#include <algorithm>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif  // OPENMP

using std::max;
using std::min;
using std::vector;
using std::pair;
using std::move;
using std::sqrt;

namespace da3d {

namespace {

Image ColorTransform(Image&& src) {
  Image img = move(src);
  if (img.channels() == 3) {
    for (int row = 0; row < img.rows(); ++row) {
      for (int col = 0; col < img.columns(); ++col) {
        float r, g, b;
        r = img.val(col, row, 0);
        g = img.val(col, row, 1);
        b = img.val(col, row, 2);
        img.val(col, row, 0) = (r + g + b) / sqrt(3);
        img.val(col, row, 1) = (r - b) / sqrt(2);
        img.val(col, row, 2) = (r - 2 * g + b) / sqrt(6);
      }
    }
  }
  return img;
}

Image ColorTransformInverse(Image&& src) {
  Image img = move(src);
  if (img.channels() == 3) {
    for (int row = 0; row < img.rows(); ++row) {
      for (int col = 0; col < img.columns(); ++col) {
        float y, u, v;
        y = img.val(col, row, 0);
        u = img.val(col, row, 1);
        v = img.val(col, row, 2);
        img.val(col, row, 0) = (sqrt(2) * y + sqrt(3) * u + v) / sqrt(6);
        img.val(col, row, 1) = (y - sqrt(2) * v) / sqrt(3);
        img.val(col, row, 2) = (sqrt(2) * y - sqrt(3) * u + v) / sqrt(6);
      }
    }
  }
  return img;
}

inline int SymmetricCoordinate(int pos, int size) {
  if (pos < 0) pos = -pos - 1;
  if (pos >= 2 * size) pos %= 2 * size;
  if (pos >= size) pos = 2 * size - 1 - pos;
  return pos;
}

pair<int, int> ComputeTiling(int rows, int columns, int tiles) {
  float best_r = sqrt(static_cast<float>(tiles * rows) / columns);
  int r_low = static_cast<int>(best_r);
  int r_up = r_low + 1;
  if (r_low < 1) return {1, tiles};
  if (r_up > tiles) return {tiles, 1};
  while (tiles % r_low != 0) --r_low;
  while (tiles % r_up != 0) ++r_up;
  if (r_up * r_low * columns > tiles * rows) {
    return {r_low, tiles / r_low};
  } else {
    return {r_up, tiles / r_up};
  }
}

vector<Image> SplitTiles(const Image &src,
                         int pad_before,
                         int pad_after,
                         pair<int, int> tiling) {
  vector<Image> result;
  for (int tr = 0; tr < tiling.first; ++tr) {
    int rstart = src.rows() * tr / tiling.first - pad_before;
    int rend = src.rows() * (tr + 1) / tiling.first + pad_after;
    for (int tc = 0; tc < tiling.second; ++tc) {
      int cstart = src.columns() * tc / tiling.second - pad_before;
      int cend = src.columns() * (tc + 1) / tiling.second + pad_after;
      Image tile(rend - rstart, cend - cstart, src.channels());
      for (int row = rstart; row < rend; ++row) {
        for (int col = cstart; col < cend; ++col) {
          for (int ch = 0; ch < src.channels(); ++ch) {
            tile.val(col - cstart, row - rstart, ch) = src.val(
                SymmetricCoordinate(col, src.columns()),
                SymmetricCoordinate(row, src.rows()),
                ch);
          }
        }
      }
      result.push_back(move(tile));
    }
  }
  return result;
}

Image MergeTiles(const vector<pair<Image, Image>> &src, pair<int, int> shape,
                 int pad_before, int pad_after, pair<int, int> tiling) {
  int channels = src[0].first.channels();
  Image result(shape.first, shape.second, channels);
  Image weights(shape.first, shape.second);
  auto tile = src.begin();
  for (int tr = 0; tr < tiling.first; ++tr) {
    int rstart = shape.first * tr / tiling.first - pad_before;
    int rend = shape.first * (tr + 1) / tiling.first + pad_after;
    for (int tc = 0; tc < tiling.second; ++tc) {
      int cstart = shape.second * tc / tiling.second - pad_before;
      int cend = shape.second * (tc + 1) / tiling.second + pad_after;
      for (int row = max(0, rstart); row < min(shape.first, rend); ++row) {
        for (int col = max(0, cstart); col < min(shape.second, cend); ++col) {
          for (int ch = 0; ch < channels; ++ch) {
            result.val(col, row, ch) +=
                tile->first.val(col - cstart, row - rstart, ch);
          }
          weights.val(col, row) += tile->second.val(col - cstart, row - rstart);
        }
      }
      ++tile;
    }
  }
  for (int row = 0; row < shape.first; ++row) {
    for (int col = 0; col < shape.second; ++col) {
      for (int ch = 0; ch < channels; ++ch) {
        result.val(col, row, ch) /= weights.val(col, row);
      }
    }
  }
  return result;
}

void ExtractPatch(const Image &src, int pr, int pc, Image *dst) {
  // src is padded, so (pr, pc) becomes the upper left pixel
  for (int row = 0; row < dst->rows(); ++row) {
    for (int col = 0; col < dst->columns(); ++col) {
      for (int chan = 0; chan < dst->channels(); ++chan) {
        dst->val(col, row, chan) = src.val(pc + col, pr + row, chan);
      }
    }
  }
}

void FastExtractPatch(const Image &src, int pr, int pc, Image *dst) {
  // src is padded, so (pr, pc) becomes the upper left pixel
  int i = 0, j = (pr * src.columns() + pc) * src.channels();
  for (int row = 0; row < dst->rows();
       ++row, j += (src.columns() - dst->columns()) * src.channels()) {
    for (int el = 0; el < dst->columns() * dst->channels(); ++el, ++i, ++j) {
      dst->val(i) = src.val(j);
    }
  }
}

void BilateralWeight(const Image &g,
                     Image *k,
                     int r,
                     float gamma_r_sigma2,
                     float sigma_s2) {
  for (int row = 0; row < g.rows(); ++row) {
    for (int col = 0; col < g.columns(); ++col) {
      float x = 0.f;
      for (int chan = 0; chan < g.channels(); ++chan) {
        float y = g.val(col, row, chan) - g.val(r, r, chan);
        x += y * y;
      }
      x /= gamma_r_sigma2;
      x += ((row - r) * (row - r) + (col - r) * (col - r)) / (2 * sigma_s2);
      k->val(col, row) = utils::fastexp(-x);
    }
  }
}

void ComputeRegressionPlane(const Image &y,
                            const Image &g,
                            const Image &k,
                            int r,
                            float reg_plane[][2]) {
  float a = 0.f, b = 0.f, c = 0.f;
  for (int row = 0; row < y.rows(); ++row) {
    for (int col = 0; col < y.columns(); ++col) {
      a += (row - r) * (row - r) * k.val(col, row);
      b += (row - r) * (col - r) * k.val(col, row);
      c += (col - r) * (col - r) * k.val(col, row);
    }
  }
  float det = a * c - b * b;
  if (det == 0) {
    for (int chan = 0; chan < y.channels(); ++chan) {
      reg_plane[chan][0] = 0.f;
      reg_plane[chan][1] = 0.f;
    }
  } else {
    for (int chan = 0; chan < y.channels(); ++chan) {
      float d = 0.f, e = 0.f;
      float central = g.val(r, r, chan);
      for (int row = 0; row < y.rows(); ++row) {
        for (int col = 0; col < y.columns(); ++col) {
          d += (row - r) * (y.val(col, row, chan) - central) * k.val(col, row);
          e += (col - r) * (y.val(col, row, chan) - central) * k.val(col, row);
        }
      }
      // Solves the system
      // |a   b| |x1|   |d|
      // |     | |  | = | |
      // |b   c| |x2|   |e|
      reg_plane[chan][0] = (c * d - b * e) / det;
      reg_plane[chan][1] = (a * e - b * d) / det;
    }
  }
}

void SubtractPlane(int r, float reg_plane[][2], Image *y) {
  for (int row = 0; row < y->rows(); ++row) {
    for (int col = 0; col < y->columns(); ++col) {
      for (int chan = 0; chan < y->channels(); ++chan) {
        y->val(col, row, chan) -=
            reg_plane[chan][0] * (row - r) + reg_plane[chan][1] * (col - r);
      }
    }
  }
}

void AddPlane(int r, float reg_plane[][2], Image *y) {
  for (int row = 0; row < y->rows(); ++row) {
    for (int col = 0; col < y->columns(); ++col) {
      for (int chan = 0; chan < y->channels(); ++chan) {
        y->val(col, row, chan) +=
            reg_plane[chan][0] * (row - r) + reg_plane[chan][1] * (col - r);
      }
    }
  }
}

void ModifyPatch(const Image &patch,
                 const Image &k,
                 DftPatch *modified,
                 float *average = nullptr) {
  // compute the total weight of the mask
  float weight = 0.f;
  for (int row = 0; row < k.rows(); ++row) {
    for (int col = 0; col < k.columns(); ++col) {
      weight += k.val(col, row);
    }
  }

  for (int chan = 0; chan < patch.channels(); ++chan) {
    float avg = 0.f;
    for (int row = 0; row < patch.rows(); ++row) {
      for (int col = 0; col < patch.columns(); ++col) {
        avg += k.val(col, row) * patch.val(col, row, chan);
      }
    }
    avg /= weight;
    for (int row = 0; row < patch.rows(); ++row) {
      for (int col = 0; col < patch.columns(); ++col) {
        modified->space(col, row, chan)[0] =
            k.val(col, row) * patch.val(col, row, chan)
                + (1 - k.val(col, row)) * avg;
        modified->space(col, row, chan)[1] = 0.f;
      }
    }
    if (average) {
      average[chan] = avg;
    }
  }
}

pair<Image, Image> DA3D_block(const Image &noisy, const Image &guide,
                              float sigma, int r, float sigma_s, float gamma_r,
                              float gamma_f, float threshold) {
  // useful values
  int s = utils::NextPowerOf2(2 * r + 1);
  float sigma2 = sigma * sigma;
  float gamma_r_sigma2 = gamma_r * sigma2;
  float sigma_s2 = sigma_s * sigma_s;

  // regression parameters
  float gamma_rr_sigma2 = gamma_r_sigma2 * 10.f;
  float sigma_sr2 = sigma_s2 * 2.f;

  // declaration of internal variables
  Image y(s, s, guide.channels());
  Image g(s, s, guide.channels());
  Image k_reg(s, s);
  Image k(s, s);
  DftPatch y_m(s, s, guide.channels());
  DftPatch g_m(s, s, guide.channels());
  int pr, pc;  // coordinates of the central pixel
  float reg_plane[guide.channels()][2];  // parameters of the regression plane
  float yt[guide.channels()];  // weighted average of the patch
  WeightMap agg_weights(guide.rows() - s + 1, guide.columns() - s + 1);  // line 1

  Image output(guide.rows(), guide.columns(), guide.channels());
  Image weights(guide.rows(), guide.columns());

  // main loop
  while (agg_weights.Minimum() < threshold) {  // line 4
    agg_weights.FindMinimum(&pr, &pc);  // line 5
    FastExtractPatch(noisy, pr, pc, &y);  // line 6
    FastExtractPatch(guide, pr, pc, &g);  // line 7
    BilateralWeight(g, &k_reg, r, gamma_rr_sigma2, sigma_sr2);  // line 8
    ComputeRegressionPlane(y, g, k_reg, r, reg_plane);  // line 9
    SubtractPlane(r, reg_plane, &y);  // line 10
    SubtractPlane(r, reg_plane, &g);  // line 11
    BilateralWeight(g, &k, r, gamma_r_sigma2, sigma_s2);  // line 12
    ModifyPatch(y, k, &y_m, yt);  // line 13
    ModifyPatch(g, k, &g_m);  // line 14
    y_m.ToFreq();  // line 15
    g_m.ToFreq();  // line 16
    float sigma_f2 = 0.f;
    for (int row = 0; row < k.rows(); ++row) {
      for (int col = 0; col < k.columns(); ++col) {
        sigma_f2 += k.val(col, row) * k.val(col, row);
      }
    }
    sigma_f2 *= sigma2;  // line 17
    for (int row = 0; row < y_m.rows(); ++row) {
      for (int col = 0; col < y_m.columns(); ++col) {
        for (int chan = 0; chan < y_m.channels(); ++chan) {
          if (row || col) {
            float G2 = g_m.freq(col, row, chan)[0] * g_m.freq(col, row, chan)[0]
                + g_m.freq(col, row, chan)[1] * g_m.freq(col, row, chan)[1];
            float K = utils::fastexp(-gamma_f * sigma_f2 / G2);  // line 18
            y_m.freq(col, row, chan)[0] *= K;
            y_m.freq(col, row, chan)[1] *= K;
          }
        }
      }
    }
    y_m.ToSpace();  // line 19

    // lines 20,21,25
    // col and row are the "internal" indexes (with respect to the patch).
    for (int row = 0; row < s; ++row) {
      for (int col = 0; col < s; ++col) {
        for (int chan = 0; chan < output.channels(); ++chan) {
          output.val(col + pc, row + pr, chan) +=
              (y_m.space(col, row, chan)[0] + (reg_plane[chan][0] * (row - r)
                  + reg_plane[chan][1] * (col - r)) * k.val(col, row)
                  - (1.f - k.val(col, row)) * yt[chan]) * k.val(col, row);
        }
        k.val(col, row) *= k.val(col, row);  // line 22
        weights.val(col + pc, row + pr) += k.val(col, row);
      }
    }
    agg_weights.IncreaseWeights(k, pr - r, pc - r);  // line 24
  }

  return {move(output), move(weights)};
}

}  // namespace

Image DA3D(const Image &noisy, const Image &guide, float sigma, int nthreads,
           int r, float sigma_s, float gamma_r, float gamma_f, float threshold) {
  // padding and color transformation
  int s = utils::NextPowerOf2(2 * r + 1);

#ifdef _OPENMP
  if (!nthreads) nthreads = omp_get_max_threads();  // number of threads
#else
  nthreads = 1;
#endif  // _OPENMP

  pair<int, int> tiling = ComputeTiling(guide.rows(), guide.columns(), nthreads);
  vector<Image> noisy_tiles = SplitTiles(ColorTransform(noisy.copy()), r, s - r - 1, tiling);
  vector<Image> guide_tiles = SplitTiles(ColorTransform(guide.copy()), r, s - r - 1, tiling);
  vector<pair<Image, Image>> result_tiles(nthreads);

#pragma omp parallel for num_threads(nthreads)
  for (int i = 0; i < nthreads; ++i) {
    result_tiles[i] = DA3D_block(noisy_tiles[i], guide_tiles[i], sigma, r,
                                 sigma_s, gamma_r, gamma_f, threshold);
  }
  return ColorTransformInverse(MergeTiles(result_tiles, guide.shape(), r, s - r - 1, tiling));
}

}  // namespace da3d