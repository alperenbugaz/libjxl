// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_AC_STRATEGY_DEBUG_H_
#define LIB_JXL_ENC_AC_STRATEGY_DEBUG_H_

#include <fstream>
#include <string>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_ac_strategy.h"
#include "lib/jxl/enc_aux_out.h"
#include "lib/jxl/enc_params.h"
#include "lib/jxl/image.h"

namespace jxl {

// Master flag for the custom debug pipeline:
//   true  -> CSV dumps + Debug variants of EstimateEntropy / FindBest*
//   false -> original libjxl flow (no I/O, no extra work)
// Flip and rebuild to toggle.
constexpr bool kIsCustomDebug = true;

// CSV output streams (defined in enc_ac_strategy_debug.cc).
extern std::ofstream dct_coeffs_file;
extern std::ofstream quantized_coeffs_file;
extern std::ofstream quant_field_file;
extern std::ofstream quant_matrices_file;
extern std::ofstream pre_quant_coeffs_file;
extern std::ofstream sparsity_cost_file;
extern std::ofstream distortion_cost_file;
extern std::ofstream pixel_distortion_file;
extern std::ofstream quant_error_file;
extern std::ofstream masking_blocks_file;
extern std::ofstream entropy_log_file;

void OpenDebugDataFiles();
void CloseDebugDataFiles();

std::string AcStrategyTypeToString(AcStrategyType type);

// One-shot dumps from AcStrategyHeuristics::Init.
void DumpMaskingParametersCSV(const Image3F& src, const ImageF& mask1x1);
void DumpMaskingBlocksCSV(const ImageF& mask1x1);

// One-shot XYB channel dump from AcStrategyHeuristics::ProcessRect.
void DumpXYBChannelsCSV(const ACSConfig& config, const Rect& rect);

// Optional .ppm visualization from AcStrategyHeuristics::Finalize.
Status DumpAcStrategyDebugVisual(const AcStrategyImage& ac_strategy,
                                 const CompressParams& cparams,
                                 AuxOut* aux_out);

}  // namespace jxl

#endif  // LIB_JXL_ENC_AC_STRATEGY_DEBUG_H_
