// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Proposed AC-strategy pipeline. kUseProposedAcStrategy true ise calisir.
// Suanki iskelet sadece DCT8 atar; mask1x1 erisimi hazirdir.

#include "lib/jxl/enc_ac_strategy_proposed.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/status.h"

namespace jxl {

bool HasMask1x1(const ACSConfig& config) {
  return config.masking1x1_field_row != nullptr && config.mask1x1_xsize > 0;
}

namespace {

// Piksel koordinatlarini gecerli mask1x1 araligina sikistirir.
size_t ClampPx(const ACSConfig& config, size_t px) {
  if (config.mask1x1_xsize == 0) return 0;
  return std::min(px, config.mask1x1_xsize - 1);
}
size_t ClampPy(const ACSConfig& config, size_t py) {
  if (config.ysize == 0) return py;
  return std::min(py, config.ysize - 1);
}

}  // namespace

float Mask1x1RegionMean(const ACSConfig& config, size_t bx, size_t by,
                        size_t size_blocks) {
  if (!HasMask1x1(config) || size_blocks == 0) return 0.0f;
  const size_t pixel_size = size_blocks * 8;
  const size_t stride = config.masking1x1_field_stride;
  const float* base = config.masking1x1_field_row;
  double sum = 0.0;
  for (size_t dy = 0; dy < pixel_size; ++dy) {
    const size_t py = ClampPy(config, by * 8 + dy);
    const float* row = base + py * stride;
    for (size_t dx = 0; dx < pixel_size; ++dx) {
      const size_t px = ClampPx(config, bx * 8 + dx);
      sum += row[px];
    }
  }
  return static_cast<float>(sum / static_cast<double>(pixel_size * pixel_size));
}

void DumpMask1x1PPM(const ACSConfig& config, const char* path) {
  if (!HasMask1x1(config) || config.ysize == 0) return;
  const size_t xsize = config.mask1x1_xsize;
  const size_t ysize = config.ysize;
  const size_t stride = config.masking1x1_field_stride;
  const float* base = config.masking1x1_field_row;

  // Normalizasyon icin min/max.
  float min_v = std::numeric_limits<float>::max();
  float max_v = std::numeric_limits<float>::lowest();
  for (size_t y = 0; y < ysize; ++y) {
    const float* row = base + y * stride;
    for (size_t x = 0; x < xsize; ++x) {
      const float v = row[x];
      if (v < min_v) min_v = v;
      if (v > max_v) max_v = v;
    }
  }
  const float range = max_v - min_v;
  const float inv_range = range > 0.0f ? 1.0f / range : 0.0f;

  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%zu %zu\n255\n", xsize, ysize);

  std::vector<uint8_t> row_buf(xsize * 3);
  for (size_t y = 0; y < ysize; ++y) {
    const float* row = base + y * stride;
    for (size_t x = 0; x < xsize; ++x) {
      float norm = (row[x] - min_v) * inv_range;
      if (norm < 0.0f) norm = 0.0f;
      if (norm > 1.0f) norm = 1.0f;
      const uint8_t g = static_cast<uint8_t>(norm * 255.0f + 0.5f);
      row_buf[x * 3 + 0] = g;
      row_buf[x * 3 + 1] = g;
      row_buf[x * 3 + 2] = g;
    }
    fwrite(row_buf.data(), 1, row_buf.size(), f);
  }
  fclose(f);
}

Status ProcessRectACSProposed(const ACSConfig& config, const Rect& rect,
                              AcStrategyImage* ac_strategy) {
  const size_t bx0 = rect.x0();
  const size_t by0 = rect.y0();
  const size_t xsize = rect.xsize();
  const size_t ysize = rect.ysize();

  // Iskelet: her bloga DCT8. Mask1x1 tabanli karar mantigi buraya gelecek.
  // Ornek erisim: Mask1x1RegionMean(config, bx, by, size_blocks)
  (void)config;

  for (size_t cy = 0; cy < ysize; ++cy) {
    for (size_t cx = 0; cx < xsize; ++cx) {
      JXL_RETURN_IF_ERROR(
          ac_strategy->Set(bx0 + cx, by0 + cy, AcStrategyType::DCT));
    }
  }
  return true;
}

}  // namespace jxl
