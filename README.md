# Flashcube

A photo album for the Nintendo GameCube, in the spirit of the Wii's Photo Channel, with a menu built like the GameCube's own: a glass cube you turn with the Control Stick.

Flashcube is homebrew. Launch `flashcube.dol` from Swiss (or any other DOL loader). It shares its music player, audio-memory tricks and SD/network setup with [CubeBrowser](https://github.com/Arial-Bold/Cubebrowser).

## What it does

- **Album:** your photos as a grid of thumbnails, with tabs for All, each year, Folders and Favorites.
- **Photo viewer:**
  - Each photo sits in a frame shaped like the photo.
  - Zoom up to 4x with L/R and move around with the stick.
  - Turn the photo, see its information, or mark it as a favourite.
  - Delete it from the SD card (it asks first).
  - Playtime opens the photo in the games.
  - A film strip shows the photos either side.
  - Z switches to full screen.
- **Slideshow:**
  - Fade, Pan & Zoom, or Cube transitions.
  - 3 to 20 seconds per photo, in order or shuffled.
  - Music from the `midi` folder, played by CubeBrowser's player.
  - The music player: press X during a slideshow (or A on the Music row before it starts) for previous / play-pause / next, shuffle, repeat, volume, and which SoundFont (`.sf2`) plays the songs.
- **Transfer:**
  - Scan the QR code on the TV with a phone and a page served by the GameCube opens.
  - From that page you can send photos to the GameCube, or browse the album and save photos to the phone.
  - A computer works too: type the address shown.
- **Playtime:**
  - **Photo Puzzle:** a sliding puzzle in 3x3, 4x4 or 5x5, with best times.
  - **Doodle:** draw on a photo.
  - **Color Looks:** Sepia, Black & White, Pop, Cool, Warm, Vintage and Negative.
  - Doodles and looks are saved as new photos. Your originals are never changed.

- **Settings** (START on the cube):
  - **Screen margin:** moves everything in from the edges of the TV (0–12%) if the top or sides are cut off. Gold corner marks show where the screen will end.
  - **Opening animation:** turn it off to go straight to the cube.
  - **Menu music:** Flashcube's own background music, on or off.
  - **16:9:** the album, viewer and other screens spread out across the wide screen.
  - **Software update:** checks GitHub for a newer Flashcube, shows what's new, and installs it (the old `.dol` is kept as `.old`). With *Check for updates at start* on, a gold tag on the cube says when there's a new version; press Z to see it.
  - **Exit button:** start another program (`sd:/boot.dol` unless you pick a different `.dol` from the card) or restart the console. B twice on the cube does the same.
- **Long text** (file names, help text) scrolls back and forth when it doesn't fit.
- **Sounds and music:** original menu sounds and a slow, airy background piece, all made by the GameCube itself as Flashcube runs. The music fades out while a song from the `midi` folder is playing.

## The SD card

```
sd:/DCIM/                 your photos, as a camera stores them:
    100CANON/IMG_0001.JPG    each folder inside DCIM is an album folder
    Camera/...               (photos straight inside DCIM work too)
    100FLASH/                photos sent from a phone, doodles and looks
sd:/midi/                 .mid songs for slideshows (shared with CubeBrowser),
                          plus an optional .sf2 SoundFont
sd:/flashcube/            Flashcube's own data (made automatically):
    settings.ini, favorites.txt, index.dat, puzzle.txt
    cacert.pem               certificates for the secure connection to GitHub
    thumbs/                  the thumbnail cache
sd:/boot.dol              started by Exit (usually Swiss). Another .dol can be
                          chosen in Settings; if the chosen one is missing,
                          boot.dol and then swiss*.dol are tried
```

JPEG, PNG, BMP and GIF files are shown. The SD card can be in an SD2SP2 (Serial Port 2) or an SD Gecko (either memory card slot).

## Controls

| Where | Control |
|---|---|
| Cube | Stick: turn to a side · A: open it · B: turn back (on the front, B twice: exit) · START: Settings · Z: a new version, when there is one |
| Album | Stick or D-pad: select · L/R: tabs · A: open · Y: slideshow · B: back |
| Viewer | D-pad left/right: previous/next · D-pad up/down + A: photo menu · L/R: zoom · Stick or C-stick: move · Z: full screen · B: back |
| Slideshow | A: pause · D-pad left/right: previous/next · X: music player · B: stop |
| Photo Puzzle | D-pad: choose a tile · A: slide it · hold Z: peek · X: size · Y: shuffle |
| Doodle | Stick: move · hold A: draw · X: colour · Y: brush · L: undo · R twice: clear · START: save |
| Color Looks | Up/down: choose a look · A: save a copy |

## Transfer

1. Connect the Broadband Adapter (or an ETH2GC-style WIZnet adapter) to your router.
2. Turn the cube to Transfer and press A.
3. Once the screen says *Ready*, scan the QR code with the phone's camera.

The phone must be on the same network.

- **Sending:** photos sent from the phone appear in the album at once, and are stored in `DCIM/100FLASH`. iPhones turn their photos into JPEGs when they send them.
- **Saving:** tap a photo on the page and press and hold it to save it to the phone's Photos, or tick several and use **Save**.
- **Address:** the address uses DHCP. For a fixed address, set `net.dhcp=0` and `net.ip`, `net.netmask` and `net.gateway` in `settings.ini`. If port 80 can't be used, the page moves to port 8080, and the address on screen shows it.

## How it stays fast on 24 MB

- **Shrinking while decoding:** big photos are decoded with TJpgDec, which shrinks a JPEG by 2, 4 or 8 while decoding. Each decoded block goes straight into the GPU's texture layout, already turned the right way up (EXIF orientation). A 12-megapixel photo never needs more memory than its final texture.
- **Camera previews:** cameras and phones store a small preview inside each JPEG (EXIF), so most thumbnails come from that instead of the whole photo. Previews padded with black bars are cropped to the photo's real shape.
- **Three levels of thumbnail cache:**
  - Main memory holds about 70.
  - Audio memory (ARAM) holds about 160 more. This uses the same interrupt-safe copy routine that fixed CubeBrowser's music.
  - Small files in `sd:/flashcube/thumbs`, so a photo is only ever decoded once.
  - Dates and sizes are kept in `index.dat`, so the album opens without reading every photo again.
- **Background loading:** a background thread loads everything, so the menu keeps moving while photos load. The viewer shows the thumbnail instantly, then the full photo.
- **Shared audio memory:** the music player keeps its instrument samples in the lower 10 MB of ARAM. The thumbnail cache uses the rest.
- **Cube drawing:** the 3D cube is drawn by placing each face's flat drawing in perspective on the CPU, so the graphics chip only ever sees the same simple drawing CubeBrowser uses.
- **Also from CubeBrowser:**
  - GPU-hang recovery.
  - The full 704-pixel picture width.
  - 480p and 16:9.
  - The controller handling: all four ports, no clamping.
  - Never using thread-local storage.

## Building

The GitHub Action (`.github/workflows/flashcube-libogc2.yml`) builds `flashcube.dol` on every push using the `ghcr.io/extremscorner/libogc2` Docker image. Download the `flashcube` artifact from the run.

**Publishing a release** (so Flashcube's updater finds it): on GitHub, make a release with a tag like `v1.1` (matching `FC_VERSION` in `source/version.h`), put the notes in its description (they're shown on the TV), and attach a `.zip` of the artifact: `flashcube.dol` at the top and the `flashcube` folder beside it.

To build by hand:

```
sh get-deps.sh          # stb_image, stb_image_write and mbedTLS into third_party/
docker run --rm -v "$PWD:/src" -w /src ghcr.io/extremscorner/libogc2:latest make
```

### Developer notes

- **PC test build:** `pc/` runs the whole app on a PC with scripted controller input, drawing through a small software GX (`pc/gxemu.c`). It writes screenshots and serves the Transfer page on `127.0.0.1`. Run `make -C pc apptest`, put test photos in `pc/sd:/DCIM/...`, then run `./apptest tour` from inside `pc/`.
- **Sound previews:** `make -C pc sfxpreview && (cd pc && ./sfxpreview)` writes every menu sound and 2½ minutes of the menu music to `pc/out/sfx/` as WAV files. The sounds are made in `source/sfxsynth.c`, the music in `source/musicsynth.c` (played by `source/menumusic.c` on one DSP voice, kept a quarter of a second ahead by a thread).
- **Fonts:** `tools/make_font.py` bakes the fonts (M PLUS Rounded 1c ExtraBold and Bold, M PLUS 1p Medium and Bold) into `source/font_data.h`.
- **Phone page:** `tools/html2c.py` turns `web/index.html` (the phone page) into `source/webpage.h`.

## Limits

- **Progressive JPEGs and PNGs:** these can only be opened up to about 2 megapixels, because there isn't enough memory to decode larger ones. Ordinary (baseline) JPEGs of any size are fine.
- **Updates:** need the network adapter and an internet connection. Releases are found at github.com/Arial-Bold/Flashcube: the newest release's tag (`v1.1`) is compared with this version, and its `.zip` (with `flashcube.dol` and the `flashcube` folder) is installed.
- **Number of photos:** up to 4,000 photos in up to 64 folders are listed.
- **Large photos:** the viewer shows photos at up to 2048 pixels on the longer side (more than the TV can show, even at 4x zoom). Slideshows use 1024.
- **Network speed:** transfers run at the GameCube network adapter's pace, roughly 100 to 300 KB/s.

## Credits and licences

- **TJpgDec** R0.03, (C) ChaN, 2021 (`source/tjpgd.*`). Free to use and redistribute with its copyright notice.
- **QR Code generator library**, (C) Project Nayuki, MIT licence (`source/qrcodegen.*`).
- **stb_image and stb_image_write**, Sean Barrett, public domain (downloaded at build time).
- **mbedTLS** 3.6, Apache 2.0 licence (downloaded at build time), for the secure connection to GitHub. `cacert.pem` is Mozilla's certificate list (MPL 2.0), as used by CubeBrowser.
- **M PLUS fonts**, (C) The M+ FONTS Project Authors, SIL Open Font License 1.1 (`tools/FONTS-OFL.txt`).
- **Music player, DOL loader, updater, secure connection, video and SD code** from CubeBrowser.
