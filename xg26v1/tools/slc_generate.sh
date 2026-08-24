#!/usr/bin/env bash
#
# Sinh lại code project (thay cho việc gọi `slc generate` trực tiếp).
#
#   tools/slc_generate.sh
#
# Làm ba việc mà gọi `slc generate` trần không làm:
#
#   1. Tự tìm đường dẫn SDK và extension aiml, rồi truyền tường minh. `slc`
#      không tự tìm ra, và đường dẫn có mã băm khác nhau trên từng máy.
#
#   2. XOÁ BẢN CACHE CŨ CỦA smart-iv-vitals.xml TRONG ~/.zap. Đây là cái bẫy đã
#      mất nhiều thời gian để tìm ra — xem mục dưới.
#
#   3. Kiểm tra sau khi sinh xong: 9 attribute ZCL tuỳ biến có còn không. Nếu
#      mất thì dừng ngay với thông báo rõ ràng, thay vì để lỗi lộ ra ở bước
#      biên dịch dưới dạng "ZCL_TS_FLAGS_ATTRIBUTE_ID undeclared".
#
# --- Cái bẫy cache của ZAP ---------------------------------------------------
#
# ZAP lưu các package ZCL đã nạp vào ~/.zap/generate.sqlite, đánh khoá theo
# ĐƯỜNG DẪN. Khi `config/zcl/smart-iv-vitals.xml` được sửa (nhóm thêm 7
# attribute cho phần dự báo), ZAP vẫn dùng bản đã cache — dù CRC đã khác:
#
#     pkg 238  crc  168642456  attr 15   <đường dẫn tuyệt đối>   ← bản CŨ, được dùng
#     pkg 239  crc 1295804267  attr 22   <đường dẫn tương đối>   ← bản ĐÚNG, bị bỏ qua
#
# Hậu quả: `autogen/zap-id.h` sinh ra thiếu 7 dòng, và build gãy. Không có cảnh
# báo nào — ZAP chạy sạch, chỉ là nó dùng định nghĩa cluster của tháng trước.
#
# Trước đây phải chữa bằng `git checkout autogen/zap-*.h` sau mỗi lần sinh.
# Script này chữa đúng nguyên nhân.

set -euo pipefail
cd "$(dirname "$0")/.."

SLC="$HOME/.silabs/slt/installs/archive/slc-cli-v6.0.20/slc_cli/slc"
ZAP_CACHE="$HOME/.zap/generate.sqlite"
BOARD="${BOARD:-brd2709a}"

# --- 1. tìm SDK + extension aiml ---------------------------------------------
SDK=$(python3 - <<'EOF'
import json, os, sys
p = os.path.expanduser("~/.silabs/sdks.json")
try:
    for sdk in json.load(open(p)):
        for e in sdk.get("extensions", []):
            if e.get("id") == "simplicity-sdk" and e.get("path"):
                print(e["path"]); sys.exit(0)
except Exception:
    pass
sys.exit(1)
EOF
) || { echo "Không tìm thấy Simplicity SDK trong ~/.silabs/sdks.json"; exit 1; }

AIML=$(ls -d "$HOME"/.silabs/slt/installs/conan/p/aiml*/p 2>/dev/null | head -1) \
  || { echo "Không tìm thấy extension aiml"; exit 1; }

echo "SDK  : $SDK"
echo "AIML : $AIML"

# --- 2. xoá bản cache cũ của XML cluster tuỳ biến -----------------------------
if [ -f "$ZAP_CACHE" ]; then
  python3 - "$ZAP_CACHE" <<'EOF'
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
n = db.execute("DELETE FROM PACKAGE WHERE PATH LIKE '%smart-iv-vitals.xml'").rowcount
db.commit()
print(f"ZAP cache: đã xoá {n} bản cache cũ của smart-iv-vitals.xml")
EOF
fi

# --- 3. sinh code -------------------------------------------------------------
"$SLC" generate -p smart-iv-monitor.slcp -d . --with "$BOARD" \
       --sdk-package-path "$SDK,$AIML"

# --- 4. kiểm tra cluster tuỳ biến còn nguyên ----------------------------------
# 7 attribute này là phần Zigbee của AI v2. Mất chúng thì build gãy, nên kiểm
# ngay ở đây chứ không để phát hiện ở bước biên dịch.
MISSING=""
for a in ZCL_TS_FLAGS_ATTRIBUTE_ID \
         ZCL_HR_FORECAST_16S_ATTRIBUTE_ID \
         ZCL_SPO2_FORECAST_16S_ATTRIBUTE_ID \
         ZCL_HR_TREND_BPM_PER_MIN_ATTRIBUTE_ID \
         ZCL_TS_ANOMALY_SCORE_X100_ATTRIBUTE_ID \
         ZCL_DROPS_FORECAST_16S_ATTRIBUTE_ID \
         ZCL_DROPS_TREND_DPM_PER_MIN_ATTRIBUTE_ID \
         ZCL_REMAINING_ML_ATTRIBUTE_ID \
         ZCL_REMAINING_MIN_ATTRIBUTE_ID; do
  grep -q "$a" autogen/zap-id.h || MISSING="$MISSING $a"
done

if [ -n "$MISSING" ]; then
  echo
  echo "LỖI: autogen/zap-id.h thiếu attribute ZCL tuỳ biến:$MISSING"
  echo
  echo "Nghĩa là ZAP lại nạp một bản smart-iv-vitals.xml cũ. Thử:"
  echo "  rm ~/.zap/generate.sqlite     # cache, ZAP tự dựng lại"
  echo "rồi chạy lại script này. Xem docs/MLTK_AUTOGEN.md mục 5."
  exit 1
fi

echo
echo "Sinh code xong. Cluster ZCL tuỳ biến còn đủ 9/9 attribute."
echo "Build: cd cmake_gcc/build && ninja"
