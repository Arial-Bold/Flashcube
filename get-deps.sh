#!/bin/sh
# Downloads the two single-file libraries the build needs into third_party/stb:
# stb_image (PNG, GIF, BMP and progressive JPEG decoding) and stb_image_write
# (saving Doodle and Color Looks pictures as JPEG). Both public domain.
set -e
cd "$(dirname "$0")"
mkdir -p third_party/stb
for f in stb_image.h stb_image_write.h; do
    if [ ! -f "third_party/stb/$f" ]; then
        echo "Downloading $f"
        curl -fL -o "third_party/stb/$f" "https://raw.githubusercontent.com/nothings/stb/master/$f"
    fi
done
echo "Done. Now run: make"
