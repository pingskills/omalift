# Omalift

Lifts a person off a photo. Open a photo, check the cutout, and save it as a PNG with a transparent background, trimmed to the person.

There are three ways to remove the background, picked with the buttons under the photo:

- **Green screen**: removes a green backdrop by its colour. Instant, and the cleanest on hair, fingers and motion blur. Chosen for you when the photo has a green screen.
- **Fast**: a model that copes with any background. Chosen for you when there is no green screen.
- **Best**: a larger model of the same kind, slower, for the fine edges Fast gets wrong.

Built the way [omacut](https://github.com/omacom-io/omacut) is: a **Qt Quick (QML)** UI with the Material style over a small C++ core, compiled to a single executable. On Omarchy, the interface follows your theme's accent color.

## Using it

Drop a photo on the window, or open one with *Ctrl+O*. The cutout is shown on a checkerboard.

- **Crop box**: drag its corners. Everything outside it is left out. On a green screen photo it starts at the edges of the cloth, which loses the room around a screen that does not fill the frame.
- **Backdrop**: change it to check the edges. A fringe that is invisible on the checkerboard shows up on white, black or pink.

The cutout is saved beside the photo as `<name>-cutout.png`, unless you choose somewhere else.

### Green screen

- **Strength**: how weak a green still counts as screen. Raise it if shadows or creases in the cloth are left behind; lower it if green-ish parts of the person disappear.
- **Edge softness**: how gradually an edge fades out. Soft suits hair and motion blur; hard suits a crisp outline.
- **Remove green tint**: takes out the green light the cloth reflects onto skin and clothes.

### Fast and Best

Each needs its model, which omalift offers to download the first time you pick it: 214 MB for Fast, 928 MB for Best. They are kept in `~/.local/share/omalift/models` and nothing is downloaded until you ask.

The model then runs once for the photo, on the processor. On a laptop without a graphics card that is roughly 20 seconds for Fast and 45 for Best. Switching between the two afterwards is instant, so it is easy to compare them.

The models are the ONNX exports of [BiRefNet](https://github.com/ZhengPeng7/BiRefNet) (MIT) published with [rembg](https://github.com/danielgatis/rembg), run by ONNX Runtime.

## Hotkeys

- *Ctrl+O*: Open a photo.
- *Ctrl+S*: Save the cutout.
- *Space*: Hold to see the original photo.
- *1 / 2 / 3*: Green screen / Fast / Best.
- *B*: Change the backdrop.
- *R*: Reset the sliders and the crop.
- *Q*: Quit.
- *?*: Show the hotkeys in the app.

## Without the window

```bash
omalift --export photo.jpg more.jpg          # green screen photos
omalift --export --fast photo.jpg more.jpg   # any background
omalift --export --best photo.jpg more.jpg   # any background, finer edges
```

Writes `<name>-cutout.png` beside each photo. Green screen photos get the settings the window opens on. `--fast` and `--best` download their model if it is not there yet.

## Taking green screen photos that key well

- Light the cloth as evenly as you can, and pull it flat. Mild creases and shadows are handled.
- Stand a step or two in front of it. That cuts both your shadow on the cloth and the green light it throws back on you.
- Wear nothing green.

## Build

Uses Qt's own build tool, `qmake6`:

```bash
./bin/build
```

This produces a single `omalift` binary in `build/`.

Requirements: a C++17 compiler, Qt6 (`qt6-base`, `qt6-declarative`) and ONNX Runtime (`onnxruntime-cpu` on Arch).

## Test

```bash
./bin/test
```

The tests that run the real models are skipped unless `OMALIFT_TEST_MODELS` (a folder holding them) and `OMALIFT_TEST_PORTRAIT` (a photo of one person) are set.

## Install

```bash
cd packaging/arch
makepkg -si
```

Builds the Arch package from the tagged release and installs it, which also adds omalift to the "Open with" menu for photos.

## License

MIT
