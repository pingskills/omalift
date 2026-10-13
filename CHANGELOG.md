# Changelog

## 0.2.0

- Fast and Best: two models that cut a person out of any background, for photos not taken against a green screen. Each downloads the first time it is picked.
- A photo with no green screen now opens in Fast instead of being turned away.
- `omalift --export --fast` and `--best` do the same without the window.
- Needs ONNX Runtime.

## 0.1.0

- Removes the green screen from a photo and saves the person as a transparent PNG, trimmed to fit.
- Sliders for strength, edge softness and removing the green tint, with a live preview.
- A crop box that starts at the edges of the cloth, for a screen that does not fill the frame.
- `omalift --export` cuts photos out without opening the window.
