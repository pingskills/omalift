# Omalift

Lifts a person off a green screen photo. Open a photo, check the cutout, and save it as a PNG with a transparent background, trimmed to the person.

Built the way [omacut](https://github.com/omacom-io/omacut) is: a **Qt Quick (QML)** UI with the Material style over a small C++ core, compiled to a single executable. On Omarchy, the interface follows your theme's accent color.

## Using it

Drop a photo on the window, or open one with *Ctrl+O*. The green is removed straight away and the cutout is shown on a checkerboard.

- **Strength**: how weak a green still counts as screen. Raise it if shadows or creases in the cloth are left behind; lower it if green-ish parts of the person disappear.
- **Edge softness**: how gradually an edge fades out. Soft suits hair and motion blur; hard suits a crisp outline.
- **Remove green tint**: takes out the green light the cloth reflects onto skin and clothes.
- **Crop box**: drag its corners. Everything outside it is left out, which is how you lose the room around a cloth that does not fill the frame. It starts at the edges of the cloth.

Change the backdrop to check the edges: a fringe that is invisible on the checkerboard shows up on white, black or pink.

The cutout is saved beside the photo as `<name>-cutout.png`, unless you choose somewhere else.

## Hotkeys

- *Ctrl+O*: Open a photo.
- *Ctrl+S*: Save the cutout.
- *Space*: Hold to see the original photo.
- *B*: Change the backdrop.
- *R*: Reset the sliders and the crop.
- *Q*: Quit.
- *?*: Show the hotkeys in the app.

## Without the window

```bash
omalift --export photo.jpg more.jpg
```

Writes `<name>-cutout.png` beside each photo, with the settings the window opens on.

## Taking photos that key well

- Light the cloth as evenly as you can, and pull it flat. Mild creases and shadows are handled.
- Stand a step or two in front of it. That cuts both your shadow on the cloth and the green light it throws back on you.
- Wear nothing green.

## What it does not do

Only green screens. A photo with an ordinary background is recognised and left alone.

## Build

Uses Qt's own build tool, `qmake6`:

```bash
./bin/build
```

This produces a single `omalift` binary in `build/`.

Requirements: a C++17 compiler and Qt6 (`qt6-base`, `qt6-declarative`).

## Test

```bash
./bin/test
```

## Install

```bash
cd packaging/arch
makepkg -si
```

Builds the Arch package from the tagged release and installs it, which also adds omalift to the "Open with" menu for photos.

## License

MIT
