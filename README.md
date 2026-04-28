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

Required build dependencies on Fedora/Bazzite-like systems:

```bash
sudo rpm-ostree install qt5-qtbase-devel qt5-qttools-devel qt5-qtwebengine-devel qt5-qtx11extras-devel hunspell-devel tesseract-devel leptonica-devel
```

If you also plan to use Wayland capture helpers on the host:

```bash
sudo rpm-ostree install slurp grim tesseract tesseract-langpack-eng python3-dbus-next
```

After layered package changes on Bazzite, reboot before building.

Typical build flow on the host system:

```bash
cd /var/home/sithond/projects/wayland-screen-translator
/usr/lib64/qt5/bin/qmake wayland-screen-translator.pro
make -j4
```

The resulting binary will be:

```bash
./wayland-screen-translator
```

Run it with:

```bash
./wayland-screen-translator
```

If you are launching the build from a Flatpak-based shell, run host tools via
`flatpak-spawn --host`, for example:

```bash
flatpak-spawn --host /usr/lib64/qt5/bin/qmake wayland-screen-translator.pro
flatpak-spawn --host make -j4
```
