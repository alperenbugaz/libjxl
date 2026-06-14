// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_ENC_AC_STRATEGY_PROPOSED_H_
#define LIB_JXL_ENC_AC_STRATEGY_PROPOSED_H_

#include <cstddef>

#include "lib/jxl/ac_strategy.h"
#include "lib/jxl/base/rect.h"
#include "lib/jxl/base/status.h"
#include "lib/jxl/enc_ac_strategy.h"

namespace jxl {

constexpr bool kUseProposedAcStrategy = false;

bool HasMask1x1(const ACSConfig& config);

float Mask1x1RegionMean(const ACSConfig& config, size_t bx, size_t by,
                        size_t size_blocks);

void Mask1x1RegionStats(const ACSConfig& config, size_t bx, size_t by,
                        size_t size_blocks, float* out_mean, float* out_std);

void DumpMask1x1PPM(const ACSConfig& config, const char* path);

Status ProcessRectACSProposed(const ACSConfig& config, const Rect& rect,
                              AcStrategyImage* ac_strategy);

}  // namespace jxl

#endif  // LIB_JXL_ENC_AC_STRATEGY_PROPOSED_H_
