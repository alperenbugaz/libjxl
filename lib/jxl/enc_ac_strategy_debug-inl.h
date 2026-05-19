// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// SIMD-templated debug variants of the AC strategy helpers
// (EstimateEntropy / FindBest8x8Transform / TryMergeAcs /
// FindBestFirstLevelDivisionForSquare). Intended to be included from inside
// the HWY_NAMESPACE block of lib/jxl/enc_ac_strategy.cc.

#include "lib/jxl/base/compiler_specific.h"

#if defined(LIB_JXL_ENC_AC_STRATEGY_DEBUG_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef LIB_JXL_ENC_AC_STRATEGY_DEBUG_INL_H_
#undef LIB_JXL_ENC_AC_STRATEGY_DEBUG_INL_H_
#else
#define LIB_JXL_ENC_AC_STRATEGY_DEBUG_INL_H_
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <vector>

#include <hwy/highway.h>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/bits.h"
#include "lib/jxl/base/fast_math-inl.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/dec_transforms-inl.h"
#include "lib/jxl/enc_ac_strategy.h"
#include "lib/jxl/enc_ac_strategy_debug.h"
#include "lib/jxl/enc_transforms-inl.h"
#include "lib/jxl/quant_weights.h"

HWY_BEFORE_NAMESPACE();
namespace jxl {
namespace HWY_NAMESPACE {

using hwy::HWY_NAMESPACE::Eq;
using hwy::HWY_NAMESPACE::IfThenZeroElse;
using hwy::HWY_NAMESPACE::Round;
using hwy::HWY_NAMESPACE::Sqrt;

// Forward declarations of helpers defined in enc_ac_strategy.cc (same
// namespace, same HWY target).
bool MultiBlockTransformCrossesHorizontalBoundary(
    const AcStrategyImage& ac_strategy, size_t start_x, size_t y, size_t end_x);
bool MultiBlockTransformCrossesVerticalBoundary(
    const AcStrategyImage& ac_strategy, size_t x, size_t start_y, size_t end_y);
AcStrategyType AcsSquare(size_t blocks);
AcStrategyType AcsVerticalSplit(size_t blocks);
AcStrategyType AcsHorizontalSplit(size_t blocks);
void SetEntropyForTransform(size_t cx, size_t cy, AcStrategyType acs_raw,
                            float entropy,
                            float* JXL_RESTRICT entropy_estimate);

// Same algorithm as EstimateEntropy, but writes per-block coefficients,
// matrices, quant field, quantized values, pre-quant values, quant error,
// pixel distortion, sparsity cost and distortion cost to CSV files, and
// returns the bit/quant/loss decomposition through extra out-params.
static Status EstimateEntropyDebug(const AcStrategy& acs, float entropy_mul,
                                   size_t x, size_t y, const ACSConfig& config,
                                   const float* JXL_RESTRICT cmap_factors,
                                   float* block, float* full_scratch_space,
                                   uint32_t* quantized, float& entropy,
                                   float& bit_cost_out, float& quant_norm_out,
                                   float& loss_scalar_out) {
  float bit_cost = 0.0f;
  float* mem = full_scratch_space;
  float* scratch_space = full_scratch_space + AcStrategy::kMaxCoeffArea;
  const size_t size = (1 << acs.log2_covered_blocks()) * kDCTBlockSize;

  for (size_t c = 0; c < 3; c++) {
    float* JXL_RESTRICT block_c = block + size * c;
    TransformFromPixels(acs.Strategy(), &config.Pixel(c, x, y),
                        config.src_stride, block_c, scratch_space);

    if (dct_coeffs_file.is_open()) {
      dct_coeffs_file << x << "," << y << "," << c << ","
                      << AcStrategyTypeToString(acs.Strategy());
      for (size_t i = 0; i < size; ++i) {
        dct_coeffs_file << "," << std::fixed << std::setprecision(8)
                        << block_c[i];
      }
      dct_coeffs_file << "\n";
    }
  }

  HWY_FULL(float) df;

  const size_t num_blocks = acs.covered_blocks_x() * acs.covered_blocks_y();
  float quant_norm16 = 0;
  if (num_blocks == 1) {
    quant_norm16 = config.Quant(x / 8, y / 8);
  } else if (num_blocks == 2) {
    if (acs.covered_blocks_y() == 2) {
      quant_norm16 =
          std::max(config.Quant(x / 8, y / 8), config.Quant(x / 8, y / 8 + 1));
    } else {
      quant_norm16 =
          std::max(config.Quant(x / 8, y / 8), config.Quant(x / 8 + 1, y / 8));
    }
  } else {
    for (size_t iy = 0; iy < acs.covered_blocks_y(); iy++) {
      for (size_t ix = 0; ix < acs.covered_blocks_x(); ix++) {
        float qval = config.Quant(x / 8 + ix, y / 8 + iy);
        qval *= qval;
        qval *= qval;
        qval *= qval;
        quant_norm16 += qval * qval;
      }
    }
    quant_norm16 /= num_blocks;
    quant_norm16 = FastPowf(quant_norm16, 1.0f / 16.0f);
  }

  const auto quant = Set(df, quant_norm16);

  const HWY_CAPPED(float, 8) df8;
  auto loss = Zero(df8);
  float unmultiplied_lossc_values[3] = {0.0f, 0.0f, 0.0f};
  for (size_t c = 0; c < 3; c++) {
    const float* inv_matrix = config.dequant->InvMatrix(acs.Strategy(), c);
    const float* matrix = config.dequant->Matrix(acs.Strategy(), c);
    if (quant_matrices_file.is_open()) {
      quant_matrices_file << x << "," << y << "," << c << ","
                          << AcStrategyTypeToString(acs.Strategy())
                          << ",matrix";
      for (size_t i = 0; i < size; ++i) {
        quant_matrices_file << "," << std::fixed << std::setprecision(8)
                            << matrix[i];
      }
      quant_matrices_file << "\n";

      quant_matrices_file << x << "," << y << "," << c << ","
                          << AcStrategyTypeToString(acs.Strategy())
                          << ",inv_matrix";
      for (size_t i = 0; i < size; ++i) {
        quant_matrices_file << "," << std::fixed << std::setprecision(8)
                            << inv_matrix[i];
      }
      quant_matrices_file << "\n";
    }

    if (quant_field_file.is_open()) {
      quant_field_file << x << "," << y << "," << c << "," << quant_norm16
                       << "\n";
    }
    const auto cmap_factor = Set(df, cmap_factors[c]);

    auto entropy_v = Zero(df);
    auto nzeros_v = Zero(df);

    std::vector<int32_t> quantized_coeffs_for_log;
    quantized_coeffs_for_log.reserve(size);

    std::vector<float> pre_quant_coeffs_for_log;
    pre_quant_coeffs_for_log.reserve(size);

    std::vector<float> quant_error_for_log;
    quant_error_for_log.reserve(size);

    for (size_t i = 0; i < num_blocks * kDCTBlockSize; i += Lanes(df)) {
      const auto in = Load(df, block + c * size + i);
      const auto in_y = Mul(Load(df, block + size + i), cmap_factor);
      const auto im = Load(df, inv_matrix + i);
      const auto val = Mul(Sub(in, in_y), Mul(im, quant));
      const auto rval = Round(val);

      HWY_ALIGN float temp_pre_quant_vals[hwy::kMaxVectorSize / sizeof(float)];
      Store(val, df, temp_pre_quant_vals);
      for (size_t j = 0; j < Lanes(df); ++j) {
        pre_quant_coeffs_for_log.push_back(temp_pre_quant_vals[j]);
      }

      HWY_ALIGN int32_t temp_quant_vals[hwy::kMaxVectorSize / sizeof(float)];
      const HWY_FULL(int32_t) di;
      Store(ConvertTo(di, rval), di, temp_quant_vals);
      for (size_t j = 0; j < Lanes(df); ++j) {
        quantized_coeffs_for_log.push_back(temp_quant_vals[j]);
      }

      const auto diff = Sub(val, rval);
      const auto m = Load(df, matrix + i);
      Store(Mul(m, diff), df, &mem[i]);
      const auto q = Abs(rval);
      const auto q_is_zero = Eq(q, Zero(df));
      entropy_v = Add(Sqrt(q), entropy_v);
      nzeros_v = Add(nzeros_v, IfThenZeroElse(q_is_zero, Set(df, 1.0f)));

      HWY_ALIGN float temp_diff_vals[hwy::kMaxVectorSize / sizeof(float)];
      Store(diff, df, temp_diff_vals);
      for (size_t j = 0; j < Lanes(df); ++j) {
        quant_error_for_log.push_back(temp_diff_vals[j]);
      }
    }

    if (pre_quant_coeffs_file.is_open()) {
      pre_quant_coeffs_file << x << "," << y << "," << c << ","
                            << AcStrategyTypeToString(acs.Strategy());
      for (const auto& coeff : pre_quant_coeffs_for_log) {
        pre_quant_coeffs_file << "," << std::fixed << std::setprecision(8)
                              << coeff;
      }
      pre_quant_coeffs_file << "\n";
    }
    if (quantized_coeffs_file.is_open()) {
      quantized_coeffs_file << x << "," << y << "," << c << ","
                            << AcStrategyTypeToString(acs.Strategy());
      for (const auto& coeff : quantized_coeffs_for_log) {
        quantized_coeffs_file << "," << coeff;
      }
      quantized_coeffs_file << "\n";
    }
    if (quant_error_file.is_open()) {
      quant_error_file << x << "," << y << "," << c << ","
                       << AcStrategyTypeToString(acs.Strategy());
      for (const auto& coeff : quant_error_for_log) {
        quant_error_file << "," << std::fixed << std::setprecision(8) << coeff;
      }
      quant_error_file << "\n";
    }

    {
      float masku_lut[3] = {12.0, 0.0, 4.0};
      auto masku_off = Set(df8, masku_lut[c]);
      auto lossc = Zero(df8);
      TransformToPixels(acs.Strategy(), &mem[0], block,
                        acs.covered_blocks_x() * 8, scratch_space);
      if (pixel_distortion_file.is_open()) {
        pixel_distortion_file << x << "," << y << "," << c << ","
                              << AcStrategyTypeToString(acs.Strategy());
        for (size_t i = 0; i < size; ++i) {
          pixel_distortion_file << "," << std::fixed << std::setprecision(8)
                                << block[i];
        }
        pixel_distortion_file << "\n";
      }
      for (size_t iy = 0; iy < acs.covered_blocks_y(); iy++) {
        for (size_t ix = 0; ix < acs.covered_blocks_x(); ix++) {
          for (size_t dy = 0; dy < kBlockDim; ++dy) {
            for (size_t dx = 0; dx < kBlockDim; dx += Lanes(df8)) {
              auto in = Load(df8, block +
                                      (iy * kBlockDim + dy) *
                                          (acs.covered_blocks_x() * kBlockDim) +
                                      ix * kBlockDim + dx);
              if (x + ix * 8 + dx + Lanes(df8) <= config.mask1x1_xsize) {
                auto masku =
                    Add(Load(df8, config.MaskingPtr1x1(x + ix * 8 + dx,
                                                       y + iy * 8 + dy)),
                        masku_off);
                in = Mul(masku, in);
                in = Mul(in, in);
                in = Mul(in, in);
                in = Mul(in, in);
                lossc = Add(lossc, in);
              }
            }
          }
        }
      }
      unmultiplied_lossc_values[c] = GetLane(SumOfLanes(df8, lossc));
      static const double kChannelMul[3] = {pow(8.2, 8.0), pow(1.0, 8.0),
                                            pow(1.03, 8.0)};
      lossc = Mul(Set(df8, kChannelMul[c]), lossc);
      loss = Add(loss, lossc);
    }
    bit_cost += config.cost_delta * GetLane(SumOfLanes(df, entropy_v));
    size_t num_nzeros = GetLane(SumOfLanes(df, nzeros_v));
    size_t nbits = CeilLog2Nonzero(num_nzeros + 1) + 1;

    if (sparsity_cost_file.is_open()) {
      sparsity_cost_file << x << "," << y << "," << c << ","
                         << AcStrategyTypeToString(acs.Strategy()) << ","
                         << num_nzeros << "," << nbits << "\n";
    }
    bit_cost += config.zeros_mul * (CeilLog2Nonzero(nbits + 17) + nbits);
    if (c == 0 && num_blocks >= 2) {
      float w = 1.0 + std::min(3.0, num_blocks / 8.0);
      bit_cost *= w;
      loss = Mul(loss, Set(df8, w));
    }
  }
  const float total_raw_loss = GetLane(SumOfLanes(df8, loss));
  float loss_scalar =
      pow(GetLane(SumOfLanes(df8, loss)) / (num_blocks * kDCTBlockSize),
          1.0f / 8.0f) *
      (num_blocks * kDCTBlockSize) / quant_norm16;
  if (distortion_cost_file.is_open()) {
    static const double kChannelMul[3] = {pow(8.2, 8.0), pow(1.0, 8.0),
                                          pow(1.03, 8.0)};
    const float final_distortion_cost =
        config.info_loss_multiplier * loss_scalar;

    distortion_cost_file << x << "," << y << ","
                         << AcStrategyTypeToString(acs.Strategy()) << ","
                         << std::fixed << std::setprecision(8)
                         << unmultiplied_lossc_values[0] << ","
                         << unmultiplied_lossc_values[1] << ","
                         << unmultiplied_lossc_values[2] << ","
                         << kChannelMul[0] << "," << kChannelMul[1] << ","
                         << kChannelMul[2] << "," << total_raw_loss << ","
                         << num_blocks * kDCTBlockSize << "," << quant_norm16
                         << "," << loss_scalar << ","
                         << config.info_loss_multiplier << ","
                         << final_distortion_cost << "\n";
  }
  bit_cost_out = bit_cost;
  quant_norm_out = quant_norm16;
  loss_scalar_out = loss_scalar;

  entropy =
      (bit_cost * entropy_mul) + (config.info_loss_multiplier * loss_scalar);
  return true;
}

static Status FindBest8x8TransformDebug(
    size_t x, size_t y, int encoding_speed_tier, float butteraugli_target,
    const ACSConfig& config, const float* JXL_RESTRICT cmap_factors,
    AcStrategyImage* JXL_RESTRICT ac_strategy, float* block,
    float* scratch_space, uint32_t* quantized, float* entropy_out,
    AcStrategyType& best_tx) {
  struct TransformTry8x8 {
    AcStrategyType type;
    int encoding_speed_tier_max_limit;
    double entropy_mul;
  };
  static const TransformTry8x8 kTransforms8x8[] = {
      {AcStrategyType::DCT, 9, 0.8},
      {AcStrategyType::DCT4X4, 5, 1.08},
      {AcStrategyType::DCT2X2, 5, 0.95},
      {AcStrategyType::DCT4X8, 4, 0.85931637428340035},
      {AcStrategyType::DCT8X4, 4, 0.85931637428340035},
      {AcStrategyType::IDENTITY, 5, 1.0427542510634957},
      {AcStrategyType::AFV0, 4, 0.81779489591359944},
      {AcStrategyType::AFV1, 4, 0.81779489591359944},
      {AcStrategyType::AFV2, 4, 0.81779489591359944},
      {AcStrategyType::AFV3, 4, 0.81779489591359944},
  };
  std::ofstream& log_file = entropy_log_file;
  static const float k8x8mul1 = -0.4;
  static const float k8x8mul2 = 1.0;
  static const float k8x8base = 1.4;
  const float mul8x8 = k8x8mul2 + k8x8mul1 / (butteraugli_target + k8x8base);

  double best = 1e30;
  best_tx = kTransforms8x8[0].type;
  for (auto tx : kTransforms8x8) {
    if (tx.encoding_speed_tier_max_limit < encoding_speed_tier) {
      continue;
    }
    AcStrategy acs = AcStrategy::FromRawStrategy(tx.type);
    float entropy_mul = tx.entropy_mul / kTransforms8x8[0].entropy_mul;

    if ((tx.type == AcStrategyType::DCT2X2 ||
         tx.type == AcStrategyType::IDENTITY) &&
        butteraugli_target < 5.0) {
      static const float kFavor2X2AtHighQuality = 0.4;
      float weight = pow((5.0f - butteraugli_target) / 5.0f, 2.0f);
      entropy_mul -= kFavor2X2AtHighQuality * weight;
    }
    if ((tx.type != AcStrategyType::DCT && tx.type != AcStrategyType::DCT2X2 &&
         tx.type != AcStrategyType::IDENTITY) &&
        butteraugli_target > 4.0) {
      static const float kAvoidEntropyOfTransforms = 0.5;
      float mul = 1.0;
      if (butteraugli_target < 12.0) {
        mul *= (12.0 - 4.0) / (butteraugli_target - 4.0);
      }
      entropy_mul += kAvoidEntropyOfTransforms * mul;
    }

    float entropy_cost;
    float bit_cost, quant_field, loss_scalar;
    JXL_RETURN_IF_ERROR(EstimateEntropyDebug(
        acs, entropy_mul, x, y, config, cmap_factors, block, scratch_space,
        quantized, entropy_cost, bit_cost, quant_field, loss_scalar));

    if (log_file.is_open()) {
      const float entropy_estimate_val = entropy_cost * mul8x8;
      log_file << x << "," << y << ","
               << AcStrategyTypeToString(acs.Strategy()) << ","
               << std::fixed << std::setprecision(6) << butteraugli_target
               << "," << encoding_speed_tier << "," << tx.entropy_mul << ","
               << entropy_mul << "," << cmap_factors[0] << ","
               << cmap_factors[2] << "," << config.info_loss_multiplier << ","
               << config.zeros_mul << "," << config.cost_delta << ","
               << bit_cost << "," << quant_field << "," << loss_scalar << ","
               << entropy_cost << "," << mul8x8 << "," << entropy_estimate_val
               << ","
               << "FB8X8"
               << "\n";
    }

    if (entropy_cost < best) {
      best_tx = tx.type;
      best = entropy_cost;
    }
  }
  *entropy_out = best;
  return true;
}

static Status TryMergeAcsDebug(
    AcStrategyType acs_raw, size_t bx, size_t by, size_t cx, size_t cy,
    const ACSConfig& config, const float* JXL_RESTRICT cmap_factors,
    AcStrategyImage* JXL_RESTRICT ac_strategy, const float entropy_mul,
    const uint8_t candidate_priority, uint8_t* priority,
    float* JXL_RESTRICT entropy_estimate, float* block, float* scratch_space,
    uint32_t* quantized, float butteraugli_target, int encoding_speed_tier) {
  AcStrategy acs = AcStrategy::FromRawStrategy(acs_raw);
  float entropy_current = 0;
  for (size_t iy = 0; iy < acs.covered_blocks_y(); ++iy) {
    for (size_t ix = 0; ix < acs.covered_blocks_x(); ++ix) {
      if (priority[(cy + iy) * 8 + (cx + ix)] >= candidate_priority) {
        return true;
      }
      entropy_current += entropy_estimate[(cy + iy) * 8 + (cx + ix)];
    }
  }

  float entropy_candidate;
  float bit_cost = 0, quant_field = 0, loss_scalar = 0;

  JXL_RETURN_IF_ERROR(EstimateEntropyDebug(
      acs, entropy_mul, (bx + cx) * 8, (by + cy) * 8, config, cmap_factors,
      block, scratch_space, quantized, entropy_candidate, bit_cost, quant_field,
      loss_scalar));

  {
    static const float k8x8mul1 = -0.4;
    static const float k8x8mul2 = 1.0;
    static const float k8x8base = 1.4;
    const float mul8x8 = k8x8mul2 + k8x8mul1 / (butteraugli_target + k8x8base);
    const float entropy_estimate_val = entropy_candidate * mul8x8;
    if (entropy_log_file.is_open()) {
      entropy_log_file << (bx + cx) * 8 << "," << (by + cy) * 8 << ","
                       << AcStrategyTypeToString(acs.Strategy()) << ","
                       << std::fixed << std::setprecision(6)
                       << butteraugli_target << "," << encoding_speed_tier
                       << "," << 1.0 << "," << entropy_mul << ","
                       << cmap_factors[0] << "," << cmap_factors[2] << ","
                       << config.info_loss_multiplier << "," << config.zeros_mul
                       << "," << config.cost_delta << "," << bit_cost << ","
                       << quant_field << "," << loss_scalar << ","
                       << entropy_candidate << "," << mul8x8 << ","
                       << entropy_estimate_val << ","
                       << "TRYMERGE"
                       << "\n";
    }
  }

  if (entropy_candidate >= entropy_current) return true;

  for (size_t iy = 0; iy < acs.covered_blocks_y(); iy++) {
    for (size_t ix = 0; ix < acs.covered_blocks_x(); ix++) {
      entropy_estimate[(cy + iy) * 8 + cx + ix] = 0;
      priority[(cy + iy) * 8 + cx + ix] = candidate_priority;
    }
  }
  JXL_RETURN_IF_ERROR(ac_strategy->Set(bx + cx, by + cy, acs_raw));
  entropy_estimate[cy * 8 + cx] = entropy_candidate;
  return true;
}

static Status FindBestFirstLevelDivisionForSquareDebug(
    size_t blocks, bool allow_square_transform, size_t bx, size_t by, size_t cx,
    size_t cy, const ACSConfig& config, const float* JXL_RESTRICT cmap_factors,
    AcStrategyImage* JXL_RESTRICT ac_strategy, const float entropy_mul_JXK,
    const float entropy_mul_JXJ, float* JXL_RESTRICT entropy_estimate,
    float* block, float* scratch_space, uint32_t* quantized,
    float butteraugli_target, int encoding_speed_tier) {
  const size_t blocks_half = blocks / 2;
  const AcStrategyType acs_rawJXK = AcsVerticalSplit(blocks);
  const AcStrategyType acs_rawKXJ = AcsHorizontalSplit(blocks);
  const AcStrategyType acs_rawJXJ = AcsSquare(blocks);

  const AcStrategy acsJXK = AcStrategy::FromRawStrategy(acs_rawJXK);
  const AcStrategy acsKXJ = AcStrategy::FromRawStrategy(acs_rawKXJ);
  const AcStrategy acsJXJ = AcStrategy::FromRawStrategy(acs_rawJXJ);

  AcStrategyRow row0 = ac_strategy->ConstRow(by + cy + 0);
  AcStrategyRow row1 = ac_strategy->ConstRow(by + cy + blocks_half);

  if (MultiBlockTransformCrossesHorizontalBoundary(*ac_strategy, bx + cx,
                                                   by + cy, bx + cx + blocks) ||
      MultiBlockTransformCrossesHorizontalBoundary(
          *ac_strategy, bx + cx, by + cy + blocks, bx + cx + blocks) ||
      MultiBlockTransformCrossesVerticalBoundary(*ac_strategy, bx + cx, by + cy,
                                                 by + cy + blocks) ||
      MultiBlockTransformCrossesVerticalBoundary(*ac_strategy, bx + cx + blocks,
                                                 by + cy, by + cy + blocks)) {
    return true;
  }

  const bool allow_JXK = !MultiBlockTransformCrossesVerticalBoundary(
      *ac_strategy, bx + cx + blocks_half, by + cy, by + cy + blocks);
  const bool allow_KXJ = !MultiBlockTransformCrossesHorizontalBoundary(
      *ac_strategy, bx + cx, by + cy + blocks_half, bx + cx + blocks);

  float entropy[2][2] = {};
  for (size_t dy = 0; dy < blocks; ++dy) {
    for (size_t dx = 0; dx < blocks; ++dx) {
      entropy[dy / blocks_half][dx / blocks_half] +=
          entropy_estimate[(cy + dy) * 8 + (cx + dx)];
    }
  }

  float entropy_JXK_left = std::numeric_limits<float>::max();
  float entropy_JXK_right = std::numeric_limits<float>::max();
  float entropy_KXJ_top = std::numeric_limits<float>::max();
  float entropy_KXJ_bottom = std::numeric_limits<float>::max();
  float entropy_JXJ = std::numeric_limits<float>::max();

  float temp_bit = 0, temp_quant = 0, temp_loss = 0;

  static const float k8x8mul1 = -0.4;
  static const float k8x8mul2 = 1.0;
  static const float k8x8base = 1.4;
  const float mul8x8 = k8x8mul2 + k8x8mul1 / (butteraugli_target + k8x8base);

  auto log_entropy_row = [&](size_t px, size_t py, AcStrategyType type,
                             float entropy_mul, float entropy_val,
                             const char* source) {
    if (!entropy_log_file.is_open()) return;
    entropy_log_file << px << "," << py << "," << AcStrategyTypeToString(type)
                     << "," << std::fixed << std::setprecision(6)
                     << butteraugli_target << "," << encoding_speed_tier << ","
                     << 1.0 << "," << entropy_mul << "," << cmap_factors[0]
                     << "," << cmap_factors[2] << ","
                     << config.info_loss_multiplier << "," << config.zeros_mul
                     << "," << config.cost_delta << "," << temp_bit << ","
                     << temp_quant << "," << temp_loss << "," << entropy_val
                     << "," << mul8x8 << "," << (entropy_val * mul8x8) << ","
                     << source << "\n";
  };

  if (allow_JXK) {
    if (row0[bx + cx + 0].Strategy() != acs_rawJXK) {
      JXL_RETURN_IF_ERROR(EstimateEntropyDebug(
          acsJXK, entropy_mul_JXK, (bx + cx + 0) * 8, (by + cy + 0) * 8, config,
          cmap_factors, block, scratch_space, quantized, entropy_JXK_left,
          temp_bit, temp_quant, temp_loss));
      log_entropy_row((bx + cx) * 8, (by + cy) * 8, acsJXK.Strategy(),
                      entropy_mul_JXK, entropy_JXK_left, "FFLD_JXK_LEFT");
    }
    if (row0[bx + cx + blocks_half].Strategy() != acs_rawJXK) {
      JXL_RETURN_IF_ERROR(EstimateEntropyDebug(
          acsJXK, entropy_mul_JXK, (bx + cx + blocks_half) * 8,
          (by + cy + 0) * 8, config, cmap_factors, block, scratch_space,
          quantized, entropy_JXK_right, temp_bit, temp_quant, temp_loss));
      log_entropy_row((bx + cx + blocks_half) * 8, (by + cy) * 8,
                      acsJXK.Strategy(), entropy_mul_JXK, entropy_JXK_right,
                      "FFLD_JXK_RIGHT");
    }
  }

  if (allow_KXJ) {
    if (row0[bx + cx].Strategy() != acs_rawKXJ) {
      JXL_RETURN_IF_ERROR(EstimateEntropyDebug(
          acsKXJ, entropy_mul_JXK, (bx + cx + 0) * 8, (by + cy + 0) * 8, config,
          cmap_factors, block, scratch_space, quantized, entropy_KXJ_top,
          temp_bit, temp_quant, temp_loss));
      log_entropy_row((bx + cx) * 8, (by + cy) * 8, acsKXJ.Strategy(),
                      entropy_mul_JXK, entropy_KXJ_top, "FFLD_KXJ_TOP");
    }
    if (row1[bx + cx].Strategy() != acs_rawKXJ) {
      JXL_RETURN_IF_ERROR(EstimateEntropyDebug(
          acsKXJ, entropy_mul_JXK, (bx + cx + 0) * 8,
          (by + cy + blocks_half) * 8, config, cmap_factors, block,
          scratch_space, quantized, entropy_KXJ_bottom, temp_bit, temp_quant,
          temp_loss));
      log_entropy_row((bx + cx) * 8, (by + cy + blocks_half) * 8,
                      acsKXJ.Strategy(), entropy_mul_JXK, entropy_KXJ_bottom,
                      "FFLD_KXJ_BOT");
    }
  }

  if (allow_square_transform) {
    JXL_RETURN_IF_ERROR(EstimateEntropyDebug(
        acsJXJ, entropy_mul_JXJ, (bx + cx + 0) * 8, (by + cy + 0) * 8, config,
        cmap_factors, block, scratch_space, quantized, entropy_JXJ, temp_bit,
        temp_quant, temp_loss));
    log_entropy_row((bx + cx) * 8, (by + cy) * 8, acsJXJ.Strategy(),
                    entropy_mul_JXJ, entropy_JXJ, "FFLD_JXJ");
  }

  float costJxN = std::min(entropy_JXK_left, entropy[0][0] + entropy[1][0]) +
                  std::min(entropy_JXK_right, entropy[0][1] + entropy[1][1]);
  float costNxJ = std::min(entropy_KXJ_top, entropy[0][0] + entropy[0][1]) +
                  std::min(entropy_KXJ_bottom, entropy[1][0] + entropy[1][1]);

  if (entropy_JXJ < costJxN && entropy_JXJ < costNxJ) {
    JXL_RETURN_IF_ERROR(ac_strategy->Set(bx + cx, by + cy, acs_rawJXJ));
    SetEntropyForTransform(cx, cy, acs_rawJXJ, entropy_JXJ, entropy_estimate);
  } else if (costJxN < costNxJ) {
    if (entropy_JXK_left < entropy[0][0] + entropy[1][0]) {
      JXL_RETURN_IF_ERROR(ac_strategy->Set(bx + cx, by + cy, acs_rawJXK));
      SetEntropyForTransform(cx, cy, acs_rawJXK, entropy_JXK_left,
                             entropy_estimate);
    }
    if (entropy_JXK_right < entropy[0][1] + entropy[1][1]) {
      JXL_RETURN_IF_ERROR(
          ac_strategy->Set(bx + cx + blocks_half, by + cy, acs_rawJXK));
      SetEntropyForTransform(cx + blocks_half, cy, acs_rawJXK,
                             entropy_JXK_right, entropy_estimate);
    }
  } else {
    if (entropy_KXJ_top < entropy[0][0] + entropy[0][1]) {
      JXL_RETURN_IF_ERROR(ac_strategy->Set(bx + cx, by + cy, acs_rawKXJ));
      SetEntropyForTransform(cx, cy, acs_rawKXJ, entropy_KXJ_top,
                             entropy_estimate);
    }
    if (entropy_KXJ_bottom < entropy[1][0] + entropy[1][1]) {
      JXL_RETURN_IF_ERROR(
          ac_strategy->Set(bx + cx, by + cy + blocks_half, acs_rawKXJ));
      SetEntropyForTransform(cx, cy + blocks_half, acs_rawKXJ,
                             entropy_KXJ_bottom, entropy_estimate);
    }
  }
  return true;
}

}  // namespace HWY_NAMESPACE
}  // namespace jxl
HWY_AFTER_NAMESPACE();

#endif  // LIB_JXL_ENC_AC_STRATEGY_DEBUG_INL_H_
