#!/bin/bash
# ==============================================================================
# Nihai Analiz ve Görselleştirme Script'i v3.0
#
# Bu script dört aşamada çalışır:
# 1. Aşama: Görüntüleri sıkıştırır, .jxl ve debug .ppm dosyalarını oluşturur.
# 2. Aşama: Oluşturulan .jxl ve .ppm dosyalarını .png formatına dönüştürür.
# 3. Aşama: Boyut tutarlılığını koruyarak karşılaştırma görselleri ve video kareleri oluşturur.
# 4. Aşama: Oluşturulan karelerden nihai karşılaştırma videosunu oluşturur.
#
# GEREKSİNİMLER:
# - ImageMagick (convert, composite, identify komutları)
# - libjxl      (cjxl ve djxl komutları)
# - FFmpeg      (ffmpeg komutu)
# ==============================================================================

# Herhangi bir komut başarısız olursa script'i anında durdur.
set -e

# --- AYARLAR (Lütfen kendi sisteminize göre düzenleyin) ---

# Sıkıştırılacak kaynak resmin yolu
INPUT_IMAGE="testdata/dots/I01.png"
# Tüm çıktıların toplanacağı ana klasör
MAIN_OUTPUT_DIR="output"
# Ara JXL ve PPM dosyalarının kaydedileceği klasör
JXL_PPM_DIR="${MAIN_OUTPUT_DIR}/jxl_ppm"
# Dönüştürülmüş PNG dosyalarının kaydedileceği klasör
PNG_DIR="${MAIN_OUTPUT_DIR}/png"
# Birleştirilmiş nihai görsellerin kaydedileceği klasör
COMPOSITE_DIR="${MAIN_OUTPUT_DIR}/birlesik"
# Geçici video karelerinin kaydedileceği klasör
TEMP_FRAME_DIR="${MAIN_OUTPUT_DIR}/gecici_video_kareleri"
# Nihai videonun kaydedileceği klasör
VIDEO_DIR="${MAIN_OUTPUT_DIR}/video"
# Kullanılacak cjxl/djxl araçlarının yolu
CJXL_TOOL="build/tools/cjxl"
DJXL_TOOL="build/tools/djxl"
# Paylaşımlı kütüphanelerin bulunduğu klasör
LIBRARY_PATH="build/lib"

# --- PARAMETRELER ---
DISSOLVE_PERCENTAGE=30 # Birleştirme sırasındaki opaklık oranı
DISTANCES=$(awk 'BEGIN{for(f=0.2; f<=2.0; f+=0.2) printf "%.1f ", f; for(f=2.5; f<=10.0; f+=0.5) printf "%.1f ", f; for(f=12; f<=25; f+=2) printf "%d ", f;}')
VIDEO_FILENAME="karsilastirma_videosu_son_hali.mp4"

# --- GÜVENLİ BEKLEME AYARLARI ---
WAIT_TIMEOUT=5
CHECK_INTERVAL=0.2

# --- SCRIPT BAŞLANGICI ---
echo "Nihai Analiz ve Görselleştirme Script'i Başlatıldı."

# --- GEREKSİNİM KONTROLLERİ ---
if ! (command -v convert &> /dev/null && command -v composite &> /dev/null && command -v identify &> /dev/null); then
    echo "HATA: Gerekli ImageMagick komutları (convert, composite, identify) bulunamadı. Lütfen kurun (apt-get install -y imagemagick)"
    exit 1
fi
if ! command -v ffmpeg &> /dev/null; then
    echo "HATA: 'ffmpeg' komutu bulunamadı. Lütfen kurun (apt-get install -y ffmpeg)"
    exit 1
fi
if [ ! -f "$CJXL_TOOL" ] || [ ! -f "$DJXL_TOOL" ]; then
    echo "HATA: cjxl veya djxl aracı bulunamadı. Lütfen projeyi derleyin."
    exit 1
fi
if [ ! -f "$INPUT_IMAGE" ]; then
    echo "HATA: Girdi resmi bulunamadı: $INPUT_IMAGE"
    exit 1
fi

# Orijinal görselin boyutunu en başta alarak standartlaştır
ORIGINAL_GEOMETRY=$(identify -format "%wx%h" "$INPUT_IMAGE")
echo "Tüm çıktılar için referans boyut belirlendi: $ORIGINAL_GEOMETRY"

# Çıktı klasör yapısını oluştur
mkdir -p "$JXL_PPM_DIR" "$PNG_DIR" "$COMPOSITE_DIR" "$TEMP_FRAME_DIR" "$VIDEO_DIR"
echo "Tüm çıktılar \"$MAIN_OUTPUT_DIR\" altındaki ilgili klasörlere kaydedilecek."

# ==============================================================================
# AŞAMA 1: SIKIŞTIRMA VE PPM OLUŞTURMA
# ==============================================================================
echo
echo "AŞAMA 1 BAŞLATILIYOR: Görüntüler sıkıştırılacak ve PPM'ler oluşturulacak..."

for distance in $DISTANCES; do
    echo "-----------------------------------------------------"
    echo "Sıkıştırma başlatılıyor: distance = $distance"
    TEMP_JXL="${JXL_PPM_DIR}/temp_output.jxl"
    DEBUG_IMAGE_ORIGINAL="ac_strategy_gorseli.ppm"
    rm -f "$DEBUG_IMAGE_ORIGINAL"
    CJXL_OUTPUT=$(LD_LIBRARY_PATH=$LIBRARY_PATH ./$CJXL_TOOL "$INPUT_IMAGE" "$TEMP_JXL" --distance="$distance" 2>&1 || true)
    echo "$CJXL_OUTPUT"
    COMPRESSED_LINE=$(echo "$CJXL_OUTPUT" | grep "Compressed to")
    if [ -n "$COMPRESSED_LINE" ]; then
        SIZE_RAW=$(echo "$COMPRESSED_LINE" | awk '{print $3}'); BPP_RAW=$(echo "$COMPRESSED_LINE" | awk '{print $5}' | tr -d '()')
        SIZE_KB=$(printf "%.0f" "$SIZE_RAW"); BPP_FORMATTED=$(printf "%.2f" "$BPP_RAW")
        BASE_FILENAME="ac_strategy_d${distance}_${BPP_FORMATTED}bpp_${SIZE_KB}kb"
        FINAL_JXL="${JXL_PPM_DIR}/${BASE_FILENAME}.jxl"; FINAL_PPM="${JXL_PPM_DIR}/${BASE_FILENAME}.ppm"
        mv "$TEMP_JXL" "$FINAL_JXL"; echo "Sıkıştırma tamamlandı: $FINAL_JXL"
        echo "PPM dosyasının diske yazılması bekleniyor..."; ATTEMPTS=$(awk -v tout="$WAIT_TIMEOUT" -v intr="$CHECK_INTERVAL" 'BEGIN{print int(tout/intr)}')
        found=false
        for (( i=1; i<=ATTEMPTS; i++ )); do
            if [ -f "$DEBUG_IMAGE_ORIGINAL" ]; then found=true; break; fi
            sleep $CHECK_INTERVAL
        done
        if [ "$found" = true ]; then
            mv "$DEBUG_IMAGE_ORIGINAL" "$FINAL_PPM"; echo "Hata ayıklama görseli kaydedildi: $FINAL_PPM"
        else
            echo "UYARI: Hata ayıklama görseli ($DEBUG_IMAGE_ORIGINAL) zaman aşımı süresinde oluşturulmadı."
        fi
    else
        echo "UYARI: cjxl çıktısı ayrıştırılamadı veya sıkıştırma başarısız oldu. Bu adım atlanıyor."; rm -f "$TEMP_JXL"
    fi
done

# ==============================================================================
# AŞAMA 2: PNG'YE DÖNÜŞTÜRME
# ==============================================================================
echo
echo "AŞAMA 2 BAŞLATILIYOR: Tüm çıktılar PNG formatına dönüştürülecek..."

# PPM -> PNG DÖNÜŞÜMÜ
echo "-----------------------------------------------------"; echo "PPM dosyaları dönüştürülüyor..."
for ppm_file in "$JXL_PPM_DIR"/*.ppm; do
    if [ -f "$ppm_file" ]; then
        base_name=$(basename "${ppm_file%.ppm}"); png_file="${PNG_DIR}/${base_name}.png"
        echo "-> \"$(basename "$ppm_file")\""; convert "$ppm_file" "$png_file"
    fi
done

# JXL -> PNG DÖNÜŞÜMÜ
echo "-----------------------------------------------------"; echo "JXL dosyaları dönüştürülüyor..."
for jxl_file in "$JXL_PPM_DIR"/*.jxl; do
    if [ -f "$jxl_file" ]; then
        base_name=$(basename "${jxl_file%.jxl}"); png_file="${PNG_DIR}/${base_name}_jxl.png"
        echo "-> \"$(basename "$jxl_file")\""; LD_LIBRARY_PATH=$LIBRARY_PATH ./$DJXL_TOOL "$jxl_file" "$png_file"
    fi
done

# ==============================================================================
# AŞAMA 3: GÖRSELLERİ BİRLEŞTİRME VE VİDEO KARELERİ OLUŞTURMA
# ==============================================================================
# ==============================================================================
# AŞAMA 3: GÖRSELLERİ BİRLEŞTİRME VE VİDEO KARELERİ OLUŞTURMA
# ==============================================================================
echo; echo "AŞAMA 3 BAŞLATILIYOR: Karşılaştırma görselleri ve video kareleri oluşturulacak..."
echo "-----------------------------------------------------"

frame_counter=0

while IFS= read -r strategy_image; do
    if [ ! -f "$strategy_image" ]; then continue; fi
    base_image_path="${strategy_image/_jxl.png/.png}"
    if [ -f "$base_image_path" ]; then
        base_filename=$(basename "$base_image_path")
        composite_output_image="$COMPOSITE_DIR/birlesik_${base_filename}"
        
        echo "Birleşik görsel oluşturuluyor: birlesik_${base_filename}"
        # DÜZELTME: ImageMagick 6 ile uyumluluk için -dissolve yerine -compose Dissolve kullanıldı.
        convert "$INPUT_IMAGE" \
                \( "$base_image_path" -resize "$ORIGINAL_GEOMETRY!" \) \
                -gravity Center \
                -compose Dissolve -define compose:args="$DISSOLVE_PERCENTAGE" \
                -composite \
                "$composite_output_image"
        
        # VİDEO KARESİNİ OLUŞTUR
        overlay_text="$base_filename"
        if [[ "$base_filename" =~ _d([0-9.]+)_([0-9.]+bpp)_([0-9]+kb) ]]; then
            distance="${BASH_REMATCH[1]}"; bpp="${BASH_REMATCH[2]}"; filesize="${BASH_REMATCH[3]}"
            overlay_text="cjxl -d${distance} (${filesize} ${bpp})"
        fi

        montage_filename=$(printf "%s/kare_%04d.png" "$TEMP_FRAME_DIR" "$frame_counter")
        echo "Video karesi oluşturuluyor: $(basename $montage_filename) (metin: '$overlay_text')"
        
        convert \( "$strategy_image" -resize "$ORIGINAL_GEOMETRY!" \) \
                \( "$composite_output_image" -resize "$ORIGINAL_GEOMETRY!" \) \
                +append \
                -gravity NorthEast -pointsize 32 -fill white -stroke black -strokewidth 1 -weight 700 \
                -annotate +20+20 "$overlay_text" \
                "$montage_filename"

        frame_counter=$((frame_counter + 1))
    else
        echo "UYARI: '$strategy_image' için temel görsel olan '$base_image_path' bulunamadı. Atlanıyor."
    fi
done < <(find "$PNG_DIR" -name '*_jxl.png' | sort -V)


# ==============================================================================
# AŞAMA 4: NİHAİ KARŞILAŞTIRMA VİDEOSUNU OLUŞTURMA
# ==============================================================================
echo
echo "AŞAMA 4 BAŞLATILIYOR: Nihai karşılaştırma videosu oluşturulacak..."
echo "-----------------------------------------------------"

if [ "$frame_counter" -eq 0 ]; then
    echo "UYARI: Video oluşturmak için hiçbir kare üretilmedi. Bu adım atlanıyor."
else
    echo "FFmpeg ile video oluşturuluyor... Kaynak: $TEMP_FRAME_DIR, Hedef: $VIDEO_DIR/$VIDEO_FILENAME"
    ffmpeg -y \
        -r 1/2 \
        -i "$TEMP_FRAME_DIR/kare_%04d.png" \
        -c:v libx264 -crf 23 -pix_fmt yuv420p \
        "$VIDEO_DIR/$VIDEO_FILENAME"
    
    echo "Video başarıyla oluşturuldu: $VIDEO_DIR/$VIDEO_FILENAME"
    
    # Geçici kareleri temizle
    echo "Geçici video kareleri temizleniyor..."
    rm -r "$TEMP_FRAME_DIR"
fi

echo "====================================================="
echo "Tüm işlemler tamamlandı. Çıktılar \"$MAIN_OUTPUT_DIR\" klasöründe."
echo "./mergeacjxl.sh"