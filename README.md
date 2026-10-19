<img width="520" height="72" alt="flashcube_logo_small" src="https://github.com/user-attachments/assets/9ba9e923-1147-4693-a9fb-3b48ac499e80" />

Flashcube a photo album for the Nintendo GameCube, in the spirit of the Wii's Photo Channel, with a menu built like the GameCube's own: a glass cube you turn with the Control Stick.

<img width="1280" height="960" alt="03_album" src="https://github.com/user-attachments/assets/230ba077-fd51-484c-bc15-da046a0b27ae" />
<img width="1280" height="960" alt="01_cube" src="https://github.com/user-attachments/assets/68a87327-72c6-481e-8519-0f7c3bcd4f0c" />

Flashcube is homebrew. Download the latest `flashcube` zip from [Releases](https://github.com/Arial-Bold/Flashcube/releases), copy it to your SD card, and launch `flashcube.dol` from Swiss or any other DOL loader.

## Features

### The cube
- A glass cube with rounded silver edges and reflections that sweep across it as it sways. Turn it with the stick to choose a side: **Album** (up), **Slideshow** (down), **Transfer** (left) or **Playtime** (right).
- An opening animation and an upbeat opening sound. You can turn the animation off to go straight to the cube.
- Original menu sounds, and Flashcube's own slow, airy background music. Both are made by the GameCube as it runs.

### Album

<img width="1280" height="960" alt="03_album" src="https://github.com/user-attachments/assets/a0b07abe-d1ba-4a42-a277-87710b19eb74" />

- All your photos as a grid of thumbnails, with tabs for **All**, each **year**, **Folders** and **Favorites**.
- Thumbnails appear quickly, even for big photos, and are remembered so each photo is only read once.

### Photo viewer
- Each photo sits in a frame shaped like the photo, with a film strip of the photos either side.
- Zoom up to 4x with L/R and move around with the stick. Z switches to full screen.
- The photo menu:
  - **Rotate** the photo.
  - **Information:** date, camera, size, file and folder.
  - **Favorite:** mark it as a favourite.
  - **Slideshow:** start a slideshow from this photo.
  - **Playtime:** use this photo in a game.
  - **Delete:** remove it from the SD card (it asks first).

### Slideshow

<img width="1280" height="960" alt="06_slideshow_setup" src="https://github.com/user-attachments/assets/78a72374-194b-41f5-8669-0fc40ab68ae8" />

- **Fade**, **Pan & Zoom** or **Cube** transitions, 3 to 20 seconds per photo, in order or shuffled.
- Music from your `midi` folder.
- Press X during a slideshow for the music player:
  - Previous, play/pause and next.
  - Shuffle, repeat and volume.
  - Which SoundFont (`.sf2`) plays the songs.

### Transfer

<img width="1280" height="960" alt="09_transfer" src="https://github.com/user-attachments/assets/e6791ebb-ef42-4cfd-b0cc-579f6687b552" />

- Scan the QR code on the TV with your phone, and a page served by the GameCube opens.
- Send photos from the phone straight into the album.
- Browse the album and save photos to the phone.
- A computer works too: type the address shown on the TV.

### Playtime

<img width="1280" height="960" alt="10_playtime" src="https://github.com/user-attachments/assets/d8adda07-60ad-4a4d-846c-a31510851b6e" />


- **Photo Puzzle:** a sliding puzzle made from any photo, in 3x3, 4x4 or 5x5, with best times.
- **Doodle:** draw on a photo with different colours and brushes.
- **Color Looks:** Sepia, Black & White, Pop, Cool, Warm, Vintage and Negative.
- Doodles and looks are saved as new photos. Your originals are never changed.

### Settings (START on the cube)

<img width="1280" height="960" alt="14_settings" src="https://github.com/user-attachments/assets/2e6ecee2-9821-4297-96d0-31e1174c7fc0" />

- **Screen:** 4:3 or 16:9. In 16:9 the screens spread out across the wide picture.
- **Picture width** (full 704 or standard 640) and **video mode** (Auto, 480i, 480p).
- **Screen margin:** moves everything in from the edges if your TV cuts them off. Gold corner marks show where the picture ends.
- **Opening animation**, **menu sounds** and **menu music**, each on or off.
- **Volume:** separate volumes for sounds and music.
- **Album order:** newest or oldest first.
- **Clear thumbnails.**
- **Software update:** checks GitHub for a new version, shows what's new, and installs it for you. The old version is kept as a `.old` file. With *Check for updates at start* on, a gold tag on the cube tells you when there's a new version; press Z to see it.
- **Exit:**
  - Start another program (`sd:/boot.dol`, usually Swiss, or any `.dol` you pick from the card), or restart the console.
  - Press B twice on the cube to exit.

### Everywhere
- Menus styled after the GameCube's own: glass panels, a fine grid and the controller hints along the bottom.
- Long names and help text scroll when they don't fit.
- Works with the official Broadband Adapter or an ETH2GC-style adapter, and an SD2SP2 or SD Gecko.

## Installing

<img width="1280" height="960" alt="15_software_update" src="https://github.com/user-attachments/assets/443e63fc-9157-4e2a-afdb-194a2a29d16f" />


1. Download the latest release zip from [Releases](https://github.com/Arial-Bold/Flashcube/releases).
2. Copy `flashcube.dol` and the `flashcube` folder to the root of your SD card.
3. Put your photos in `DCIM` (see below) and start `flashcube.dol` from Swiss.

From then on, Flashcube can update itself from **Settings > Software update**.

## The SD card

```
sd:/DCIM/                 your photos, as a camera stores them:
    100CANON/IMG_0001.JPG    each folder inside DCIM is an album folder
    Camera/...               (photos straight inside DCIM work too)
    100FLASH/                photos sent from a phone, doodles and looks
sd:/midi/                 .mid songs for slideshows (shared with CubeBrowser),
                          plus an optional .sf2 SoundFont
sd:/flashcube/            Flashcube's own folder:
    cacert.pem               certificates for checking GitHub for updates
    README.txt
    settings.ini, favorites.txt, index.dat, puzzle.txt, thumbs/   (made automatically)
sd:/boot.dol              started by Exit (usually Swiss)
```

JPEG, PNG, BMP and GIF photos are shown.

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

## Using Transfer

1. Connect the network adapter to your router.
2. Turn the cube to **Transfer** and press A.
3. Once the screen says *Ready*, scan the QR code with your phone's camera. The phone must be on the same network.

- **Sending photos:** photos you send appear in the album at once and are stored in `DCIM/100FLASH`. iPhones turn their photos into JPEGs as they send them.
- **Saving photos:** tap a photo on the page, then press and hold it to save it to your phone, or tick several and use **Save**.
- **Fixed address:** to use one instead of DHCP, set `net.dhcp=0`, `net.ip`, `net.netmask` and `net.gateway` in `sd:/flashcube/settings.ini`.

## Good to know

- **Large photos:** ordinary JPEGs of any size open fine. Progressive JPEGs and PNGs open up to about 2 megapixels, because larger ones don't fit in the GameCube's memory.
- **How many photos:** up to 4,000 photos in up to 64 folders.
- **Transfer speed:** about 100 to 300 KB/s, the network adapter's pace.
- **Updates:** need the network adapter and an internet connection.

## Credits

- **TJpgDec**, (C) ChaN.
- **QR Code generator library**, (C) Project Nayuki, MIT licence.
- **stb_image and stb_image_write**, Sean Barrett, public domain.
- **mbedTLS**, Apache 2.0 licence.
- **cacert.pem:** Mozilla's certificate list, MPL 2.0.
- **M PLUS fonts**, (C) The M+ FONTS Project Authors, SIL Open Font License 1.1.
- **Shared code:** the music player, DOL loader, updater, secure connection, video and SD card code come from [CubeBrowser](https://github.com/Arial-Bold/Cubebrowser).
- The menu sounds, the background music and the cube's look are Flashcube's own. Nothing from Nintendo is included.

Flashcube was built with **Claude** (by Anthropic), working with Arial-Bold.
