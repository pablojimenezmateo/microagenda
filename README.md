# microagenda

microagenda is a small, fast, Linux-only agenda app built with C++20, SDL3, SQLite, and the Markdown/editor pieces copied from `micronotes`.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```sh
./build/bin/microagenda --agenda /path/to/agenda
```

If `--agenda` is omitted, the app uses `$HOME/.local/share/microagenda`.

The agenda database lives at `.microagenda/agenda.sqlite` under the chosen root. Pasted image attachments are stored under `.microagenda/attachments/<note-id>/`.

## Install a Desktop Launcher

Install into a user prefix so the app appears in the application menu:

```sh
cmake --install build --prefix ~/.local
```

This installs:

- `microagenda` into `~/.local/bin`
- a `.desktop` launcher into `~/.local/share/applications`
- the app icon into `~/.local/share/icons/hicolor/scalable/apps`

## Build an Installer Package

Create a Debian package from the build tree:

```sh
cmake --build build --target package
```

The package is written to the build directory and includes the desktop launcher and icon.

## Controls

- `Ctrl+N`: create an entry.
- `Ctrl+Shift+N`: create a note for the selected entry.
- `Ctrl+F`: focus entry search.
- Search scope toggle: `A` searches all entry content; `N` searches names only.
- `Ctrl+S`: save the active edit.
- `Ctrl+L`: cycle note editor/view/split mode.
- Mouse wheel: scroll the left column or selected-entry detail pane.

See [docs/markdown-elements.md](docs/markdown-elements.md) for the Markdown renderer fixture.
