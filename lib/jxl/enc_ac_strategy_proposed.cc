#include "lib/jxl/enc_ac_strategy_proposed.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/status.h"

namespace jxl {


constexpr float kKMean64   = 1.00f;
constexpr float kKStd64    = 0.30f;
constexpr float kKMean32   = 0.50f;
constexpr float kKStd32    = 0.50f;
constexpr float kKMean16   = 0.00f;
constexpr float kKStd16    = 0.75f;
constexpr float kKMeanEdge = 0.75f;  // region_mean < μ - 0.75σ → kenar
constexpr AcStrategyType kEdgeTransform = AcStrategyType::DCT2X2;

bool HasMask1x1(const ACSConfig& config) {
  return config.masking1x1_field_row != nullptr && config.mask1x1_xsize > 0;
}

namespace {

size_t ClampPx(const ACSConfig& config, size_t px) {
  if (config.mask1x1_xsize == 0) return 0;
  return std::min(px, config.mask1x1_xsize - 1);
}
size_t ClampPy(const ACSConfig& config, size_t py) {
  if (config.ysize == 0) return py;
  return std::min(py, config.ysize - 1);
}

struct GlobalStats {
  const float* base = nullptr;
  float mean = 0.0f;
  float std = 0.0f;
};

GlobalStats ComputeGlobalStats(const ACSConfig& config) {
  GlobalStats s;
  if (!HasMask1x1(config)) return s;
  const size_t xsize = config.mask1x1_xsize;
  const size_t ysize = config.ysize;
  const size_t stride = config.masking1x1_field_stride;
  s.base = config.masking1x1_field_row;
  double sum = 0.0;
  double sum2 = 0.0;
  size_t count = 0;
  for (size_t y = 0; y < ysize; ++y) {
    const float* row = s.base + y * stride;
    for (size_t x = 0; x < xsize; ++x) {
      const float v = row[x];
      sum += v;
      sum2 += static_cast<double>(v) * v;
      ++count;
    }
  }
  if (count == 0) return s;
  const double mean = sum / static_cast<double>(count);
  const double var =
      std::max(0.0, sum2 / static_cast<double>(count) - mean * mean);
  s.mean = static_cast<float>(mean);
  s.std = static_cast<float>(std::sqrt(var));
  return s;
}

// Mask1x1 base pointer degisince (yeni gorsel) yeniden hesaplar.
const GlobalStats& GetCachedGlobalStats(const ACSConfig& config) {
  static GlobalStats stats;
  if (stats.base != config.masking1x1_field_row) {
    stats = ComputeGlobalStats(config);
    fprintf(stderr, "[proposed] mask1x1 global mean=%.4f std=%.4f\n",
            stats.mean, stats.std);
  }
  return stats;
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

void Mask1x1RegionStats(const ACSConfig& config, size_t bx, size_t by,
                        size_t size_blocks, float* out_mean, float* out_std) {
  *out_mean = 0.0f;
  *out_std = 0.0f;
  if (!HasMask1x1(config) || size_blocks == 0) return;
  const size_t pixel_size = size_blocks * 8;
  const size_t stride = config.masking1x1_field_stride;
  const float* base = config.masking1x1_field_row;
  double sum = 0.0;
  double sum2 = 0.0;
  size_t count = 0;
  for (size_t dy = 0; dy < pixel_size; ++dy) {
    const size_t py = ClampPy(config, by * 8 + dy);
    const float* row = base + py * stride;
    for (size_t dx = 0; dx < pixel_size; ++dx) {
      const size_t px = ClampPx(config, bx * 8 + dx);
      const float v = row[px];
      sum += v;
      sum2 += static_cast<double>(v) * v;
      ++count;
    }
  }
  if (count == 0) return;
  const double mean = sum / static_cast<double>(count);
  const double var =
      std::max(0.0, sum2 / static_cast<double>(count) - mean * mean);
  *out_mean = static_cast<float>(mean);
  *out_std = static_cast<float>(std::sqrt(var));
}

void DumpMask1x1PPM(const ACSConfig& config, const char* path) {
  if (!HasMask1x1(config) || config.ysize == 0) return;
  const size_t xsize = config.mask1x1_xsize;
  const size_t ysize = config.ysize;
  const size_t stride = config.masking1x1_field_stride;
  const float* base = config.masking1x1_field_row;

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

namespace {

constexpr size_t kGroupBlocks = 8;
constexpr size_t kIdx(size_t cx, size_t cy) { return cy * kGroupBlocks + cx; }

bool IsHomogeneous(const ACSConfig& config, const GlobalStats& g,
                   size_t bx, size_t by, size_t size_blocks,
                   float k_mean, float k_std) {
  if (!HasMask1x1(config)) return false;
  if (g.std <= 0.0f) return true;
  float mean = 0.0f;
  float std = 0.0f;
  Mask1x1RegionStats(config, bx, by, size_blocks, &mean, &std);
  const float mean_thr = g.mean + k_mean * g.std;
  const float std_thr = k_std * g.std;
  return mean > mean_thr && std < std_thr;
}

bool IsEdgeCell(const ACSConfig& config, const GlobalStats& g,
                size_t bx, size_t by) {
  if (!HasMask1x1(config) || g.std <= 0.0f) return false;
  float mean = 0.0f;
  float std = 0.0f;
  Mask1x1RegionStats(config, bx, by, 1, &mean, &std);
  const float mean_thr = g.mean - kKMeanEdge * g.std;
  return mean < mean_thr;
}

bool ChunkFree(const bool* taken, size_t cx, size_t cy, size_t size) {
  for (size_t iy = 0; iy < size; ++iy) {
    for (size_t ix = 0; ix < size; ++ix) {
      if (taken[kIdx(cx + ix, cy + iy)]) return false;
    }
  }
  return true;
}

void MarkChunk(bool* taken, size_t cx, size_t cy, size_t size) {
  for (size_t iy = 0; iy < size; ++iy) {
    for (size_t ix = 0; ix < size; ++ix) {
      taken[kIdx(cx + ix, cy + iy)] = true;
    }
  }
}

}  // namespace

Status ProcessRectACSProposed(const ACSConfig& config, const Rect& rect,
                              AcStrategyImage* ac_strategy) {
  const size_t bx0 = rect.x0();
  const size_t by0 = rect.y0();
  const size_t xsize = rect.xsize();
  const size_t ysize = rect.ysize();

  const GlobalStats& g = GetCachedGlobalStats(config);
  bool taken[kGroupBlocks * kGroupBlocks] = {};

  for (size_t cy = 0; cy < ysize; ++cy) {
    for (size_t cx = 0; cx < xsize; ++cx) {
      if (IsEdgeCell(config, g, bx0 + cx, by0 + cy)) {
        JXL_RETURN_IF_ERROR(
            ac_strategy->Set(bx0 + cx, by0 + cy, kEdgeTransform));
        taken[kIdx(cx, cy)] = true;
      }
    }
  }

  if (xsize == kGroupBlocks && ysize == kGroupBlocks &&
      ChunkFree(taken, 0, 0, kGroupBlocks) &&
      IsHomogeneous(config, g, bx0, by0, kGroupBlocks, kKMean64, kKStd64)) {
    JXL_RETURN_IF_ERROR(
        ac_strategy->Set(bx0, by0, AcStrategyType::DCT64X64));
    MarkChunk(taken, 0, 0, kGroupBlocks);
  }

  for (size_t cy = 0; cy + 4 <= ysize; cy += 4) {
    for (size_t cx = 0; cx + 4 <= xsize; cx += 4) {
      if (!ChunkFree(taken, cx, cy, 4)) continue;
      if (IsHomogeneous(config, g, bx0 + cx, by0 + cy, 4,
                        kKMean32, kKStd32)) {
        JXL_RETURN_IF_ERROR(ac_strategy->Set(
            bx0 + cx, by0 + cy, AcStrategyType::DCT32X32));
        MarkChunk(taken, cx, cy, 4);
      }
    }
  }

  for (size_t cy = 0; cy + 2 <= ysize; cy += 2) {
    for (size_t cx = 0; cx + 2 <= xsize; cx += 2) {
      if (!ChunkFree(taken, cx, cy, 2)) continue;
      if (IsHomogeneous(config, g, bx0 + cx, by0 + cy, 2,
                        kKMean16, kKStd16)) {
        JXL_RETURN_IF_ERROR(ac_strategy->Set(
            bx0 + cx, by0 + cy, AcStrategyType::DCT16X16));
        MarkChunk(taken, cx, cy, 2);
      }
    }
  }

  for (size_t cy = 0; cy < ysize; ++cy) {
    for (size_t cx = 0; cx < xsize; ++cx) {
      if (!taken[kIdx(cx, cy)]) {
        JXL_RETURN_IF_ERROR(
            ac_strategy->Set(bx0 + cx, by0 + cy, AcStrategyType::DCT));
      }
    }
  }
  return true;
}

}  // namespace jxl
