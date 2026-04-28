# Wayland Screen Translator

[Русская версия](README.ru.md)

Wayland-native OCR and translation tool for screen regions.

This project is prepared as a clean base for a new repository and local
development. Old repository links and update endpoints were removed on purpose.

Based on the original ScreenTranslator project, with further Wayland-focused
adaptation and maintenance by sithond.

## Current Focus

- region capture on Linux/Wayland
- OCR with Tesseract
- optional translation through script-based translators
- floating translation window

## Running

The application lives in the system tray after launch.

Basic flow:

1. Start the app.
2. Choose a capture region.
3. Wait for OCR and translation updates.
4. Reuse the same translation window as text changes.

## Notes

- Update URLs are intentionally disabled until a new repository is configured.
- Upstream attribution is preserved in `LICENSE.md`.
- Generated build artifacts should not be committed to the new repository.
- This English document and the Russian document can evolve together as the
  project grows.

## Build

The project uses Qt 5, Tesseract, Leptonica, Hunspell, and WebEngine.

Typical build flow:

```bash
/usr/lib64/qt5/bin/qmake wayland-screen-translator.pro
make -j4
```
