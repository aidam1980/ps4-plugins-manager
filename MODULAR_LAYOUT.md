# PS4PluginEditor — modular source layout

The source has been separated by responsibility. The Makefile already compiles every `*.cpp` file inside `PS4PluginEditor/`, so the new modules are picked up automatically.

## Source modules

- `main.cpp` — application startup, controller polling, event loop, and dispatch to the active screen.
- `app_state.h` / `app_state.cpp` — shared data types, shared state, constants, and declarations used across modules.
- `utils.cpp` — reusable string, path, and small parsing helpers.
- `games.cpp` — installed-game discovery, metadata/title reading, icon lookup, and page icon caching.
- `plugins.cpp` — plugin discovery/catalog, per-game plugin state, tab selection, and bulk assignment.
- `plugins_ini.cpp` — `plugins.ini` reading/writing, line edits/deletes, and backup-list/restore operations.
- `ime_keyboard.cpp` — native PS4 system IME dialog flow for editing a selected `plugins.ini` line.
- `ui_draw.cpp` — drawing helpers and the screen layouts.
- `jbc_bootstrap.cpp` — existing JBC bootstrap; unchanged.
- `png_decode.h` — existing PNG decoder; unchanged.

## Build

From the project root, use the same OpenOrbis environment and build command you used before:

```sh
make clean
make
```

`make clean` removes build outputs and `make` recompiles the split modules. The current environment used to prepare this source archive did not have the OpenOrbis toolchain installed, so this package has **not** been verified by a complete PS4-target build here. Run the commands above in your configured OpenOrbis environment and share the first compiler error if anything appears.

No gameplay/UI behavior was intentionally changed as part of this file split; the goal is separation of responsibilities only.
