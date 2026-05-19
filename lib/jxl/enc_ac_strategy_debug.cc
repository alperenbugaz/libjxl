// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Custom debug pipeline for AC strategy selection: CSV dumps, file handles,
// and one-shot debug visualizations. All of this code is dead when
// kIsCustomDebug is false (see enc_ac_strategy_debug.h).

#include "lib/jxl/enc_ac_strategy_debug.h"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <ios>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/common.h"
#include "lib/jxl/enc_xyb.h"
#include "lib/jxl/image.h"

namespace jxl {

std::ofstream dct_coeffs_file;
std::ofstream quantized_coeffs_file;
std::ofstream quant_field_file;
std::ofstream quant_matrices_file;
std::ofstream pre_quant_coeffs_file;
std::ofstream sparsity_cost_file;
std::ofstream distortion_cost_file;
std::ofstream pixel_distortion_file;
std::ofstream quant_error_file;
std::ofstream masking_blocks_file;
std::ofstream entropy_log_file;

void OpenDebugDataFiles() {
  entropy_log_file.open("entropy_log.csv", std::ios::trunc);
  if (entropy_log_file.is_open()) {
    entropy_log_file << "BlokX,BlokY,AcStrategyType,butteraugli_target,"
                     << "EncodingSpeedTier,Temel_entropy_mul,entropy_mul,"
                     << "CMapFaktor_X,CMapFaktor_B,Info_Loss,"
                     << "Zeros_Mul,CostDelta,Kodlama_Maliyeti,quant_norm16,"
                     << "Loss_Scalar,Maliyet,mul8x8,Son_Maliyet,Source\n";
  }
  dct_coeffs_file.open("debug_dct_coeffs.csv", std::ios::trunc);
  sparsity_cost_file.open("debug_sparsity_cost.csv", std::ios::trunc);
  quantized_coeffs_file.open("debug_quantized_coeffs.csv", std::ios::trunc);
  quant_field_file.open("debug_quant_field.csv", std::ios::trunc);
  quant_matrices_file.open("debug_quant_matrices.csv", std::ios::trunc);
  pre_quant_coeffs_file.open("debug_pre_quant_coeffs.csv", std::ios::trunc);
  distortion_cost_file.open("debug_distortion_cost.csv", std::ios::trunc);
  pixel_distortion_file.open("debug_pixel_distortion.csv", std::ios::trunc);
  quant_error_file.open("debug_quant_error.csv", std::ios::trunc);
  masking_blocks_file.open("debug_masking_blocks.csv", std::ios::trunc);
  if (masking_blocks_file.is_open()) {
    masking_blocks_file << "BlockX,BlockY,MaskValues(64)\n";
  }
  if (pixel_distortion_file.is_open()) {
    pixel_distortion_file << "BlockX,BlockY,Channel,TransformType,Coeffs(64)\n";
  }
  if (quant_error_file.is_open()) {
    quant_error_file << "BlockX,BlockY,Channel,TransformType,QuantError(64)\n";
  }
  if (sparsity_cost_file.is_open()) {
    sparsity_cost_file
        << "BlockX,BlockY,Channel,TransformType,NumNonZeros,NBits\n";
  }
  if (distortion_cost_file.is_open()) {
    distortion_cost_file << "BlockX,BlockY,TransformType,"
                         << "RawLoss_X,RawLoss_Y,RawLoss_B,"
                         << "ChannelMul_X,ChannelMul_Y,ChannelMul_B,"
                         << "TotalRawLoss,PixelCount,QuantField,"
                         << "LossScalar,InfoLossMultiplier,FinalDistortionCost\n";
  }
  if (pre_quant_coeffs_file.is_open()) {
    pre_quant_coeffs_file
        << "BlockX,BlockY,Channel,TransformType,PreQuantCoeffs(64)\n";
  }
  if (dct_coeffs_file.is_open()) {
    dct_coeffs_file << "BlockX,BlockY,Channel,TransformType,Coeffs(64)\n";
  }
  if (quantized_coeffs_file.is_open()) {
    quantized_coeffs_file
        << "BlockX,BlockY,Channel,TransformType,QuantizedCoeffs(64)\n";
  }
  if (quant_field_file.is_open()) {
    quant_field_file << "BlockX,BlockY,Channel,QuantField\n";
  }
  if (quant_matrices_file.is_open()) {
    quant_matrices_file
        << "BlockX,BlockY,Channel,TransformType,MatrixType,Values(64)\n";
  }
}

void CloseDebugDataFiles() {
  if (dct_coeffs_file.is_open()) dct_coeffs_file.close();
  if (quantized_coeffs_file.is_open()) quantized_coeffs_file.close();
  if (quant_field_file.is_open()) quant_field_file.close();
  if (quant_matrices_file.is_open()) quant_matrices_file.close();
  if (pre_quant_coeffs_file.is_open()) pre_quant_coeffs_file.close();
  if (sparsity_cost_file.is_open()) sparsity_cost_file.close();
  if (distortion_cost_file.is_open()) distortion_cost_file.close();
  if (pixel_distortion_file.is_open()) pixel_distortion_file.close();
  if (quant_error_file.is_open()) quant_error_file.close();
  if (masking_blocks_file.is_open()) masking_blocks_file.close();
  if (entropy_log_file.is_open()) entropy_log_file.close();
}

std::string AcStrategyTypeToString(AcStrategyType type) {
  switch (type) {
    case AcStrategyType::DCT: return "DCT";
    case AcStrategyType::IDENTITY: return "IDENTITY";
    case AcStrategyType::DCT2X2: return "DCT2X2";
    case AcStrategyType::DCT4X4: return "DCT4X4";
    case AcStrategyType::DCT16X16: return "DCT16X16";
    case AcStrategyType::DCT32X32: return "DCT32X32";
    case AcStrategyType::DCT16X8: return "DCT16X8";
    case AcStrategyType::DCT8X16: return "DCT8X16";
    case AcStrategyType::DCT32X8: return "DCT32X8";
    case AcStrategyType::DCT8X32: return "DCT8X32";
    case AcStrategyType::DCT32X16: return "DCT32X16";
    case AcStrategyType::DCT16X32: return "DCT16X32";
    case AcStrategyType::DCT4X8: return "DCT4X8";
    case AcStrategyType::DCT8X4: return "DCT8X4";
    case AcStrategyType::AFV0: return "AFV0";
    case AcStrategyType::AFV1: return "AFV1";
    case AcStrategyType::AFV2: return "AFV2";
    case AcStrategyType::AFV3: return "AFV3";
    case AcStrategyType::DCT64X64: return "DCT64X64";
    case AcStrategyType::DCT64X32: return "DCT64X32";
    case AcStrategyType::DCT32X64: return "DCT32X64";
    case AcStrategyType::DCT128X128: return "DCT128X128";
    case AcStrategyType::DCT128X64: return "DCT128X64";
    case AcStrategyType::DCT64X128: return "DCT64X128";
    case AcStrategyType::DCT256X256: return "DCT256X256";
    case AcStrategyType::DCT256X128: return "DCT256X128";
    case AcStrategyType::DCT128X256: return "DCT128X256";
    default: return "UNKNOWN";
  }
}

namespace {

// Scalar copies of the masking helpers from enc_adaptive_quantization.cc,
// inlined here so the dump in DumpMaskingParametersCSV does not have to
// reach into the SIMD-templated originals.
constexpr float kSGmul = 226.77216153508914f;
constexpr float kSGmul2 = 1.0f / 73.377132366608819f;
constexpr float kSGRetMul = kSGmul2 * 18.6580932135f * kInvLog2e;
constexpr float kSGVOffset = 7.7825991679894591f;

float RatioOfDerivativesOfCubicRootToSimpleGamma(float v) {
  const float kEpsilon = 1e-2f;
  if (v < 0.0f) v = 0.0f;
  const float kNumMul = kSGRetMul * 3.0f * kSGmul;
  const float kVOffset = kSGVOffset * kInvLog2e + kEpsilon;
  const float kDenMul = kInvLog2e * kSGmul;
  const float v2 = v * v;
  const float num = kNumMul * v2 + kEpsilon;
  const float den = (kDenMul * v) * v2 + kVOffset;
  return den / num;
}

float MaskingSqrt(float v) {
  const float kLogOffset = 27.505837037000106f;
  const float kMul = 211.66567973503678f;
  return 0.25f * std::sqrt(v * std::sqrt(kMul * 1e8f) + kLogOffset);
}

}  // namespace

void DumpMaskingParametersCSV(const Image3F& src, const ImageF& mask1x1) {
  std::ofstream dump_file("debug_masking_parameters.csv", std::ios::trunc);
  if (!dump_file.is_open()) return;

  dump_file << "BlockX,BlockY,Type";
  for (int i = 0; i < 64; ++i) dump_file << ",Val" << i;
  dump_file << "\n";

  const size_t xsize = src.xsize();
  const size_t ysize = src.ysize();
  const size_t xsize_blocks = DivCeil(xsize, kBlockDim);
  const size_t ysize_blocks = DivCeil(ysize, kBlockDim);

  const float match_gamma_offset = 0.019f;
  const float kLimit = 0.2f;

  for (size_t by = 0; by < ysize_blocks; ++by) {
    for (size_t bx = 0; bx < xsize_blocks; ++bx) {
      dump_file << bx << "," << by << ",MASK1X1";
      for (size_t dy = 0; dy < 8; ++dy) {
        for (size_t dx = 0; dx < 8; ++dx) {
          size_t x = bx * 8 + dx;
          size_t y = by * 8 + dy;
          float val = (x < mask1x1.xsize() && y < mask1x1.ysize())
                          ? mask1x1.Row(y)[x]
                          : 0.0f;
          dump_file << "," << std::fixed << std::setprecision(8) << val;
        }
      }
      dump_file << "\n";

      dump_file << bx << "," << by << ",DIFF1";
      for (size_t dy = 0; dy < 8; ++dy) {
        const size_t y = by * 8 + dy;
        const size_t y_c = y >= ysize ? ysize - 1 : y;
        const size_t y_t = y_c > 0 ? y_c - 1 : 0;
        const size_t y_b = y_c + 1 < ysize ? y_c + 1 : y_c;
        const float* row_c = src.ConstPlaneRow(1, y_c);
        const float* row_t = src.ConstPlaneRow(1, y_t);
        const float* row_b = src.ConstPlaneRow(1, y_b);
        for (size_t dx = 0; dx < 8; ++dx) {
          const size_t x = bx * 8 + dx;
          const size_t x_c = x >= xsize ? xsize - 1 : x;
          const size_t x_l = x_c > 0 ? x_c - 1 : 0;
          const size_t x_r = x_c + 1 < xsize ? x_c + 1 : x_c;
          float val_c = row_c[x_c];
          float base =
              0.25f * (row_t[x_c] + row_b[x_c] + row_c[x_l] + row_c[x_r]);
          float gammac = RatioOfDerivativesOfCubicRootToSimpleGamma(
              val_c + match_gamma_offset);
          float diff1 = std::abs(gammac * (val_c - base));
          dump_file << "," << diff1;
        }
      }
      dump_file << "\n";

      dump_file << bx << "," << by << ",DIFF2_RECALC";
      for (size_t dy = 0; dy < 8; ++dy) {
        const size_t y = by * 8 + dy;
        const size_t y_c = y >= ysize ? ysize - 1 : y;
        const size_t y_t = y_c > 0 ? y_c - 1 : 0;
        const size_t y_b = y_c + 1 < ysize ? y_c + 1 : y_c;
        const float* row_c = src.ConstPlaneRow(1, y_c);
        const float* row_t = src.ConstPlaneRow(1, y_t);
        const float* row_b = src.ConstPlaneRow(1, y_b);
        for (size_t dx = 0; dx < 8; ++dx) {
          const size_t x = bx * 8 + dx;
          const size_t x_c = x >= xsize ? xsize - 1 : x;
          const size_t x_l = x_c > 0 ? x_c - 1 : 0;
          const size_t x_r = x_c + 1 < xsize ? x_c + 1 : x_c;
          float val_c = row_c[x_c];
          float base =
              0.25f * (row_t[x_c] + row_b[x_c] + row_c[x_l] + row_c[x_r]);
          float gammac = RatioOfDerivativesOfCubicRootToSimpleGamma(
              val_c + match_gamma_offset);
          float diff = gammac * (val_c - base);
          diff *= diff;
          if (diff >= kLimit) diff = kLimit;
          diff = MaskingSqrt(diff);
          dump_file << "," << diff;
        }
      }
      dump_file << "\n";
    }
  }
}

void DumpMaskingBlocksCSV(const ImageF& mask1x1) {
  static bool mask_blocks_saved = false;
  if (!masking_blocks_file.is_open() || mask_blocks_saved ||
      mask1x1.xsize() == 0) {
    return;
  }
  const size_t xsize_blocks = DivCeil(mask1x1.xsize(), kBlockDim);
  const size_t ysize_blocks = DivCeil(mask1x1.ysize(), kBlockDim);

  for (size_t by = 0; by < ysize_blocks; ++by) {
    for (size_t bx = 0; bx < xsize_blocks; ++bx) {
      masking_blocks_file << bx << "," << by;
      for (size_t dy = 0; dy < kBlockDim; ++dy) {
        size_t y = by * kBlockDim + dy;
        if (y >= mask1x1.ysize()) continue;
        const float* row = mask1x1.Row(y);
        for (size_t dx = 0; dx < kBlockDim; ++dx) {
          size_t x = bx * kBlockDim + dx;
          float val = (x < mask1x1.xsize()) ? row[x] : 0.0f;
          masking_blocks_file << "," << std::fixed << std::setprecision(8)
                              << val;
        }
      }
      masking_blocks_file << "\n";
    }
  }
  mask_blocks_saved = true;
}

void DumpXYBChannelsCSV(const ACSConfig& config, const Rect& rect) {
  static bool xyb_data_saved = false;
  if (xyb_data_saved) return;
  printf("XYB renk kanallari CSV dosyalarina kaydediliyor...\n");
  const size_t xsize = rect.xsize() * kBlockDim;
  const size_t ysize = rect.ysize() * kBlockDim;
  SaveChannelToCSV("XYB_X.csv", "X", config.src_rows[0], xsize, ysize,
                   config.src_stride);
  SaveChannelToCSV("XYB_Y.csv", "Y (Luma)", config.src_rows[1], xsize, ysize,
                   config.src_stride);
  SaveChannelToCSV("XYB_B.csv", "B", config.src_rows[2], xsize, ysize,
                   config.src_stride);
  xyb_data_saved = true;
}

}  // namespace jxl
