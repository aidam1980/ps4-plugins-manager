# Game Plugins behavior changes

- Press X on a plugin to open the game list. The list is initialized from the plugin's current effective state for each installed game, so games where the plugin is already enabled are visibly marked `PLUGIN ENABLED`.
- In the game list, press X on a game to toggle that plugin for that game immediately. No Triangle confirmation is required. Circle returns to the plugin list.
- In the plugin list/grid, press OPTIONS to toggle the selected plugin's `[default]` state in `plugins.ini`.
- When setting a plugin to default ON, stale per-game entries for that same plugin are removed so the default applies across all apps. Subsequent per-game toggles can still create an explicit override.

Source changes are included. Build with the project's OpenOrbis toolchain before installing on PS4; this archive has not been tested on-console in this environment.
