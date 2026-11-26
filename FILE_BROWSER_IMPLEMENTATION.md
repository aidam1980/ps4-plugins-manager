# File Browser implementation notes

- Added GET view 3 for the local File Browser.
- The browser opens at `/` and supports directory navigation.
- X enters folders or imports a selected PRX/SPRX into `/data/GoldHEN/plugins`.
- Matching PNG artwork is copied alongside the plugin when present.
- L1 toggles multi-selection for PRX/SPRX entries.
- Square asks for confirmation before deleting the selected item(s); deleting a plugin also removes its matching PNG.
- Triangle on a folder opens `MOVE PLUGINS` / `ADD TO GET`.
- `MOVE PLUGINS` recursively scans the selected folder for PRX/SPRX files and copies matching PNGs.
- If target plugin filenames already exist, the browser asks before replacing them.
- `ADD TO GET` saves the folder path in `/data/GoldHEN/plugins_manager_sources.txt`.
- Saved folder shortcuts appear after the three built-in GET entries and reopen that folder in the browser.
- INTERNET remains a placeholder.

The code has not been built with the OpenOrbis toolchain in this environment. Please run `make clean && make` and test on the PS4 before relying on it.
\n\n## Navigation and current-folder actions (Stage 3)\n- D-pad and left analog directions now move immediately, then repeat while held (18-frame initial delay, 5-frame repeat interval at the main loop frame rate).\n- Triangle in File Browser opens folder actions for the directory currently displayed, not the highlighted child item. MOVE PLUGINS and ADD TO GET both use the current directory path.\n

## Stage 4 updates
- Game Plugins entries are sorted alphabetically by plugin filename (case-insensitive), with full path as a tie-breaker.
- Opening a saved GET shortcut enables shortcut-return mode: Circle returns directly to the GET list instead of walking up the directory tree. The regular File Browser retains normal parent-folder navigation.
- Square on a user-saved GET shortcut asks for confirmation and removes only that saved path. The built-in INTERNET, USB, and FILE BROWSER entries are protected and cannot be deleted.
