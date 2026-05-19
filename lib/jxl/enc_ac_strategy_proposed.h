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

// Proposed pipeline'i ac/kapat. true -> kendi akisimiz calisir.
constexpr bool kUseProposedAcStrategy = true;

// mask1x1 var mi?
bool HasMask1x1(const ACSConfig& config);

// (bx, by) blogundan baslayan size_blocks x size_blocks 'lik bolgenin
// mask1x1 ortalamasini dondurur. Koordinatlar 8x8 blok cinsindendir.
float Mask1x1RegionMean(const ACSConfig& config, size_t bx, size_t by,
                        size_t size_blocks);

// mask1x1 haritasini grayscale PPM olarak kaydeder (min siyah, max beyaz).
void DumpMask1x1PPM(const ACSConfig& config, const char* path);

// 64x64 piksellik bir grup icin blok stratejilerini doldurur.
// Suanlik iskelet: her bloga DCT8 atar. Asil karar mantigi ilerde
// bu fonksiyonun govdesine yazilacak.
Status ProcessRectACSProposed(const ACSConfig& config, const Rect& rect,
                              AcStrategyImage* ac_strategy);

}  // namespace jxl

#endif  // LIB_JXL_ENC_AC_STRATEGY_PROPOSED_H_
