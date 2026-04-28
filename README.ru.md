# Wayland Screen Translator

[English version](README.md)

Wayland-native OCR и переводчик текста из выделенной области экрана.

Этот проект подготовлен как чистая база для нового репозитория и дальнейшей
локальной разработки. Старые ссылки на репозиторий и старые update endpoint’ы
специально убраны.

Проект основан на оригинальном ScreenTranslator и дальше адаптируется под
Wayland и поддерживается sithond.

## Текущий фокус

- захват области на Linux/Wayland
- OCR через Tesseract
- опциональный перевод через script-based translators
- плавающее окно перевода

## Запуск

После старта приложение живёт в системном трее.

Базовый сценарий:

1. Запустить приложение.
2. Выбрать область захвата.
3. Дождаться OCR и обновления перевода.
4. Использовать одно и то же окно перевода, пока текст меняется.

## Заметки

- URL обновлений временно отключены, пока не будет настроен новый репозиторий.
- Атрибуция исходного upstream сохранена в `LICENSE.md`.
- Сгенерированные build-артефакты не стоит коммитить в новый репозиторий.
- Эта русская версия и английская версия README могут дополняться по мере
  развития проекта.

## Сборка

Проект использует Qt 5, Tesseract, Leptonica, Hunspell и WebEngine.

Зависимости для сборки на Fedora/Bazzite-подобных системах:

```bash
sudo rpm-ostree install qt5-qtbase-devel qt5-qttools-devel qt5-qtwebengine-devel qt5-qtx11extras-devel hunspell-devel tesseract-devel leptonica-devel
```

Если вы также хотите использовать Wayland-захват на хосте:

```bash
sudo rpm-ostree install slurp grim tesseract tesseract-langpack-eng python3-dbus-next
```

После установки layered-пакетов на Bazzite нужно перезагрузиться.

Типичный процесс сборки на хост-системе:

```bash
cd /var/home/sithond/projects/wayland-screen-translator
/usr/lib64/qt5/bin/qmake wayland-screen-translator.pro
make -j4
```

Готовый бинарник будет здесь:

```bash
./wayland-screen-translator
```

Запуск:

```bash
./wayland-screen-translator
```

Если вы запускаете сборку из Flatpak-окружения, вызывайте host-инструменты
через `flatpak-spawn --host`, например:

```bash
flatpak-spawn --host /usr/lib64/qt5/bin/qmake wayland-screen-translator.pro
flatpak-spawn --host make -j4
```
