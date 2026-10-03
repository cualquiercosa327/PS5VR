#!/usr/bin/env bash
# Builds the overlay preview on the Mac: the app's real interface code
# (ui_*, app_osd*, app_subs, app_session) against Homebrew's FreeType,
# HarfBuzz, fribidi, libass, FFmpeg and image libraries. ./osd_preview then
# renders each interface state to out/*.png. Needs:
#   brew install freetype harfbuzz fribidi libass ffmpeg libpng jpeg-turbo webp
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; APP="$(cd "$HERE/../.." && pwd)"
B=/opt/homebrew/opt
mkdir -p "$HERE/build" "$HERE/out"
# The eboot embeds assets with .incbin (ELF sections); here they are read from disk.
python3 - "$APP" "$HERE/build/ui_assets_host.c" <<'PY'
import re, sys
app, out = sys.argv[1], sys.argv[2]
src = open(app + "/src/ui_assets.c").read()
pairs = re.findall(r'app_blob_(\w+):\\n\.incbin \\"([^"\\]+)\\"', src)
c = ['#include "ui_assets.h"', '#include <stdio.h>', '#include <stdlib.h>', '',
     'static ui_asset load(const char *path) {',
     '    ui_asset a = {0, 0}; FILE *f = fopen(path, "rb"); if (!f) return a;',
     '    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);',
     '    unsigned char *d = malloc(n + 1); fread(d, 1, n, f); d[n] = 0; fclose(f);',
     '    a.data = d; a.size = (size_t)n; return a; }', '']
for name, path in pairs:
    c.append('ui_asset ui_asset_%s(void) { static ui_asset a; if (!a.data) a = load("%s/%s"); return a; }' % (name, app, path))
open(out, "w").write("\n".join(c) + "\n")
PY
INC="-I$APP/src -I$APP/third_party -I$APP/engine/include -I$APP/engine/addons/include \
  -I$B/freetype/include/freetype2 -I$B/harfbuzz/include/harfbuzz -I$B/fribidi/include/fribidi \
  -I$B/libass/include -I$B/ffmpeg/include -I$B/libpng/include -I$B/jpeg-turbo/include -I$B/webp/include"
LIBS="-L$B/freetype/lib -lfreetype -L$B/harfbuzz/lib -lharfbuzz -L$B/fribidi/lib -lfribidi \
  -L$B/libass/lib -lass -L$B/ffmpeg/lib -lavformat -lavcodec -lavutil -L$B/libpng/lib -lpng \
  -L$B/jpeg-turbo/lib -ljpeg -L$B/webp/lib -lwebp -lz"
C="$APP/src"
for f in ui_canvas ui_text ui_icons ui_image; do
  clang -O2 -g -w -std=gnu11 $INC -c "$C/$f.c" -o "$HERE/build/$f.o"
done
clang -O2 -w $INC -c "$HERE/build/ui_assets_host.c" -o "$HERE/build/ui_assets_host.o"
INC="$INC -I$APP/engine/media/include"
clang++ -O2 -g -w -std=c++17 -DAPP_VR=1 $INC -c "$C/vr_ui.cpp" -o "$HERE/build/vr_ui.o"
clang++ -O2 -g -w -std=c++17 $INC -c "$HERE/vr_ui_preview.cpp" -o "$HERE/build/vr_ui_preview.o"
rm -f "$HERE/build/cJSON.o"
clang++ -o "$HERE/vr_ui_preview" "$HERE"/build/*.o $LIBS
echo "built $HERE/vr_ui_preview"
