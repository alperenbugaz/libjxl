# Custom AC Strategy Pipeline

Bu döküman libjxl encoder'a eklenmiş iki bağımsız özelleştirme katmanını anlatır.

1. **Debug pipeline (`kIsCustomDebug`)** — orijinal AC strateji seçim akışına CSV log + PPM görselleştirme enjekte eder.
2. **Proposed pipeline (`kUseProposedAcStrategy`)** — orijinal akışın yerine mask1x1-tabanlı kendi blok seçim mantığını çalıştırır.

Her iki katman da varsayılan olarak kapalıdır; tek satır `constexpr` flip + yeniden derleme ile aktif olur.

---

## Dosya yapısı

```
lib/jxl/
  enc_ac_strategy.cc              # Upstream akış + iki bayrağa göre routing
  enc_ac_strategy.h               # ACSConfig (xsize/ysize alanları eklendi)
  enc_ac_strategy_debug.h         # kIsCustomDebug + debug API decl
  enc_ac_strategy_debug.cc        # ofstream globals, Open/Close, masking dump
  enc_ac_strategy_debug-inl.h     # SIMD'li Debug varyantları
                                  #   EstimateEntropyDebug
                                  #   FindBest8x8TransformDebug
                                  #   TryMergeAcsDebug
                                  #   FindBestFirstLevelDivisionForSquareDebug
  enc_ac_strategy_proposed.h      # kUseProposedAcStrategy + mask1x1 helpers
  enc_ac_strategy_proposed.cc     # ProcessRectACSProposed + DumpMask1x1PPM
```

`lib/jxl_lists.cmake` içinde tüm yeni dosyalar `JPEGXL_INTERNAL_ENC_SOURCES` setine eklenmiştir.

---

## Bayraklar

| Bayrak | Dosya | Default | Etkisi |
|---|---|---|---|
| `kIsCustomDebug` | `enc_ac_strategy_debug.h` | `false` | CSV dump + Debug fonksiyon varyantları çalışır |
| `kUseProposedAcStrategy` | `enc_ac_strategy_proposed.h` | `false` | `ProcessRect` orijinal `HWY_DYNAMIC_DISPATCH(ProcessRectACS)` yerine `ProcessRectACSProposed`'a yönlenir |

### Etkileşim matrisi

| `kIsCustomDebug` | `kUseProposedAcStrategy` | Davranış |
|---|---|---|
| false | false | Vanilla upstream libjxl |
| true  | false | Upstream + per-block CSV + `ac_strategy_gorseli.ppm` + `mask1x1.ppm` |
| false | true  | Proposed pipeline + `ac_strategy_proposed.ppm` + `mask1x1.ppm` |
| true  | true  | Init/Finalize debug aktif + proposed pipeline çalışır. **Per-call CSV'ler boş kalır** çünkü orijinal `EstimateEntropy*` akışı çalışmıyor. |

---

## Akış değişiklikleri

### `ProcessRectACS` (HWY namespace, `enc_ac_strategy.cc`)

Her `FindBest8x8Transform` / `FindBestFirstLevelDivisionForSquare` / `TryMergeAcs` çağrısı şu kalıba sarıldı:

```cpp
if (kIsCustomDebug) {
  JXL_RETURN_IF_ERROR(FindBest8x8TransformDebug(...));
} else {
  JXL_RETURN_IF_ERROR(FindBest8x8Transform(...));
}
```

`kIsCustomDebug = false` iken else branch'i upstream'in birebir kopyası.

### `AcStrategyHeuristics::Init`

```cpp
if (kIsCustomDebug) {
  OpenDebugDataFiles();
  DumpMaskingParametersCSV(src, mask1x1);
  DumpMaskingBlocksCSV(mask1x1);
}
// ...
config.xsize = src.xsize();   // proposed pipeline'in mask bound-check'i icin
config.ysize = src.ysize();
```

### `AcStrategyHeuristics::ProcessRect`

```cpp
if (cparams.speed_tier >= SpeedTier::kCheetah) {
  ac_strategy->FillDCT8(rect);
  return true;
}
if (kUseProposedAcStrategy) {
  return ProcessRectACSProposed(config, rect, ac_strategy);
}
return HWY_DYNAMIC_DISPATCH(ProcessRectACS)(...);
```

### `AcStrategyHeuristics::Finalize`

```cpp
if (kIsCustomDebug) CloseDebugDataFiles();

if (kIsCustomDebug || kUseProposedAcStrategy) {
  DumpAcStrategy(...);                       // ac_strategy_*.ppm
  DumpMask1x1PPM(config, "mask1x1.ppm");     // importance map
}
```

---

## CSV çıktıları (`kIsCustomDebug = true`)

### Init'te tek seferlik

| Dosya | İçerik |
|---|---|
| `debug_masking_parameters.csv` | Her blok için MASK1X1 / DIFF1 / DIFF2_RECALC değerleri (3 satır × 64 değer) |
| `debug_masking_blocks.csv` | Her blok için mask1x1 değerleri (1 satır × 64 değer) |

### Her `EstimateEntropyDebug` çağrısında

| Dosya | İçerik |
|---|---|
| `debug_dct_coeffs.csv` | DCT katsayıları |
| `debug_quantized_coeffs.csv` | Kuantize edilmiş katsayılar (int) |
| `debug_pre_quant_coeffs.csv` | Kuantizasyon öncesi ölçeklenmiş katsayılar |
| `debug_quant_error.csv` | Kuantizasyon hatası (val − rval) |
| `debug_quant_matrices.csv` | matrix ve inv_matrix |
| `debug_quant_field.csv` | quant_norm16 değeri |
| `debug_pixel_distortion.csv` | IDCT sonrası pixel uzayda bozulma |
| `debug_sparsity_cost.csv` | num_nzeros + nbits |
| `debug_distortion_cost.csv` | Kanal başına raw loss + nihai distortion cost |

### Strateji denemelerinden

| Dosya | İçerik |
|---|---|
| `entropy_log.csv` | Denenen her strateji için satır (BlokX, BlokY, AcStrategyType, butteraugli_target, maliyet bileşenleri, mul8x8, son maliyet, Source) |

### Boyut uyarısı

5000×5000 image'da debug açıkken toplam CSV ~180–200 GB civarı olabilir. Sadece **küçük test image'leri** (256×256, 512×512) için kullan.

---

## PPM görselleri

| Dosya | Üretildiği koşul | İçerik |
|---|---|---|
| `ac_strategy_gorseli.ppm` | `kIsCustomDebug = true` ve `kUseProposedAcStrategy = false` | Upstream algoritmasının seçtiği blok stratejileri (renkli, 16-bit P6) |
| `ac_strategy_proposed.ppm` | `kUseProposedAcStrategy = true` | Proposed pipeline'ın seçtiği blok stratejileri (renkli, 16-bit P6) |
| `mask1x1.ppm` | İki bayraktan biri true | Önem haritası, grayscale (siyah = düşük, beyaz = yüksek), linear min/max normalize, 8-bit P6 |

Renk kodlaması için `enc_ac_strategy.cc` içindeki `TypeColor` tablosuna bak (DCT=sarı, DCT16X16=chartreuse, DCT32X32=yeşil, vb.).

---

## Proposed pipeline API

### mask1x1 erişimi (`enc_ac_strategy_proposed.h`)

```cpp
bool  HasMask1x1(const ACSConfig& config);
float Mask1x1RegionMean(const ACSConfig& config, size_t bx, size_t by,
                        size_t size_blocks);
void  DumpMask1x1PPM(const ACSConfig& config, const char* path);
```

Koordinat sistemi:
- `(bx, by)` = block koordinatı (1 blok = 8×8 pixel)
- Out-of-range erişim en yakın geçerli sample'a clamp'lenir
- mask1x1 yoksa `Mask1x1RegionMean` 0 döner (null-safe)

`Mask1x1RegionMean(bx, by, n)` — block-aligned `n×n` blok karenin mask1x1 ortalaması.

### Strateji seçimi

```cpp
Status ProcessRectACSProposed(const ACSConfig& config, const Rect& rect,
                              AcStrategyImage* ac_strategy);
```

**Top-down quadtree** ile çalışır. Tek pipeline, tek karar fonksiyonu.

```
PlaceQuadtree(bx, by, size=8):       // 64x64 piksel
  importance = Mask1x1RegionMean(bx, by, size)
  if importance < ImportanceThreshold(size):
    Set big block at (bx, by)
  else:
    PlaceQuadtree(bx,        by,        size/2)
    PlaceQuadtree(bx+size/2, by,        size/2)
    PlaceQuadtree(bx,        by+size/2, size/2)
    PlaceQuadtree(bx+size/2, by+size/2, size/2)
```

`ImportanceThreshold` mevcut değerleri (cameraman.png için kalibre,
mask1x1 ~3..89 aralığı, mean ~49):

| size_blocks | Transform | Threshold |
|---|---|---|
| 8 | DCT64X64 | 25.0 |
| 4 | DCT32X32 | 40.0 |
| 2 | DCT16X16 | 55.0 |
| 1 | DCT (8×8) | ∞ (leaf, hep kabul) |

Quadtree garanti eder:
- **Overlap yok** — disjoint quadrant'lar
- **Hizalama otomatik** — halving aligned pozisyonlar üretir
- **Sınır taşması yok** — 8×8'lik olmayan rect'ler (image kenar) için fallback'te tümü DCT8

İlk `ProcessRectACSProposed` çağrısında terminale tek seferlik diagnostic basılır:
```
[proposed] mask1x1 stats: min=X.XX mean=Y.YY max=Z.ZZ (n=...)
[proposed] thresholds: size8=... size4=... size2=...
```
Threshold kalibrasyonu için bu çıktıyı kullan.

---

## Mimari kararlar

### Neden `-inl.h`?

Debug fonksiyonları SIMD'li (Google Highway). libjxl'nin `<hwy/foreach_target.h>` pattern'i bu fonksiyonları her SIMD hedefi için yeniden derler. Convention: header dosyasına koy, main `.cc`'ye `#include` et (`enc_transforms-inl.h`, `dec_transforms-inl.h` ile aynı pattern).

### Neden proposed `-inl.h` değil?

Proposed pipeline şu an SIMD-bağımsız (sadece skaler aritmetik). Tek `.cc` yeterli. Daha sonra performans için SIMD ile yeniden yazılırsa o zaman `-inl.h`'e taşınabilir.

### Neden `constexpr bool`?

- Derleme zamanı sabit → compiler dead code eliminate eder, runtime overhead = 0
- Bayrak `false` iken debug/proposed kodu binary'de olsa bile asla çalışmaz
- Tek satır flip + yeniden derle → trade-off kabul edilebilir (debug için runtime toggle gerekmez)

---

## Build

```bash
cd build
cmake --build .
```

Bayrak değişikliğinden sonra `enc_ac_strategy.cc` ve dahil ettiği header'lar dirty olur → otomatik recompile.

---

## Test örneği

```bash
cd build
./tools/cjxl ../testimages/cameraman.png output.jxl -d 2.0 -e 7 --num_threads 1
```

`kUseProposedAcStrategy = true` ise oluşan dosyalar:
- `output.jxl`
- `ac_strategy_proposed.ppm`
- `mask1x1.ppm`

Görsel karşılaştırma: iki PPM'i yan yana aç. mask1x1'de parlak (yüksek aktivite) bölgelerde proposed pipeline küçük blok seçmiş olmalı; karanlık (smooth) bölgelerde büyük blok.

---

## Bilinen sınırlamalar / TODO

- [ ] `ImportanceThreshold` değerleri şu an cameraman.png için sabit — dinamik (image-adaptive percentile-driven) hale getirilmeli
- [ ] Multi-metric karar (mean + max veya variance) eklenebilir; tek mean smooth görünen ama içinde keskin nokta olan region'larda büyük blok seçer
- [ ] Butteraugli-aware threshold ölçeklemesi (yüksek kalite → daha küçük blok bias'ı)
- [ ] `SaveChannelToCSV` artık çağrılmıyor ama `enc_xyb.h/.cc`'de duruyor (ileride başka debug için lazım olabilir)
- [ ] Per-call CSV'ler büyük image'da pratik değil → ya küçük image kullan ya da blok-range filtresi ekle
- [ ] `kIsCustomDebug=true && kUseProposedAcStrategy=true` kombinasyonunda per-call CSV'ler boş kalır (bilinçli — orijinal `EstimateEntropy*` çağrılmıyor)

---

## Upstream ile davranış farkı

`kIsCustomDebug = false && kUseProposedAcStrategy = false` iken davranış upstream libjxl ile **birebir aynı**. Kalan kod farkları:

- Türkçe ALPCOM yorumları (`EstimateEntropy` ve `ProcessRectACS` içinde)
- Kozmetik formatlamalar
- `AcsSquare`'de hiç çağrılmayan `blocks == 1` case'i (ölü dal)
- `SetEntropyForTransform` imzasından `static` kaldırıldı (forward declaration için)
- `ACSConfig::xsize/ysize` alanları (Init'te set ediliyor, vanilla flow'da okunmuyor)

Hiçbiri encode sonucunu etkilemez.
