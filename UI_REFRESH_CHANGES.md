# UI refresh (1.14 candidate)

Drawing code only: `ui_draw.cpp`, `get.cpp` (DrawGetMenu / DrawGetUsb), palette in `app_state.cpp/.h`.
No input handling, file I/O or plugin logic was changed.

- New UI toolkit in `ui_draw.cpp`: rounded shapes, soft shadows, selection glow, pill badges,
  scrollbars, page dots, pixel-accurate text measuring (`TextWidth`, `ShortenToWidth`).
- `DrawPanel` is now rounded with a shadow, so every screen (including Internet catalog and
  file browser, which were not edited) picks up the new look automatically.
- Top bar: gradient, logo mark, JBC status badge, right-aligned breadcrumb.
- Tabs: shorter, centred labels, accent underline on the active tab.
- Game/plugin cards: bordered icon frames, glow ring on selection, green edge + badge for
  games with the plugin enabled, titles trimmed to real pixel width instead of a character guess.
- Lists: header strip, zebra rows, status dots, state badges, scrollbar, more room (panel moved up).
- plugins.ini: line-number gutter, colour key (section / found / missing), scrollbar.
- Keyboard: text field with caret, rounded keys, SPACE labelled.
- Legend bar: hairline separators, truly centred labels ("ASSIGN TO GAMES" shortened to "ASSIGN").
- Status line shows a coloured dot; long messages are trimmed instead of running off screen.

Not compiled with the OpenOrbis toolchain here (not installed). The drawing code was compiled on a
host with a stub `Scene2D` and rendered to PNGs to check layout. Build with `make clean && make`
and send the first compiler error if any appears.

## Round 2 (from console screenshots)
- Text measuring returned 0 on the console, so labels were not centred, long titles were not trimmed and the
  plugins.ini colour key overlapped. `TextWidth` now caches per-glyph advances and falls back to an estimate.
- "PAGE x / y" is now on the left; on the plugin grid the status message moves to the right to make room.
- Game details list shows plugin file names only (no /data/GoldHEN/plugins/ path).
- Plugin status grid: games that already have the plugin show an "Already selected" line under the game id.
- plugins.ini colour key uses fixed positions.
- Legend/footer icons moved up so they no longer overlap their labels.
- Card shadows are now thin strips instead of full-size shapes (less drawing per frame).

## Round 3: My Games search + faster hold-to-scroll
- SQUARE on My Games opens the native PS4 system keyboard (same IME dialog and init sequence as the
  plugins.ini line editor, see `PromptSearchText` in ime_keyboard.cpp). Matches game title or title ID,
  case-insensitive; several words must all match. Confirming an empty text clears the search.
- With a search active the grid shows only matches, the header shows "3 of 56 games" and the search text,
  CIRCLE clears it. Opening a game, "selected plugins" etc. all work on the filtered list.
- Hold-to-repeat is now time-based (it was counted in frames, and the UI runs at ~12 FPS so it was very slow):
  repeat starts after 0.26 s, speeds up after 0.9 s and again after 2.2 s, and can move several items per frame.
  Applies to every screen.
- While a direction is auto-repeating, game icons for newly reached pages load once you slow down/stop
  (avoids decoding 9 PNGs every few steps).
- After the system keyboard closes, the pad state is re-synced so a still-held button isn't read as a new press.

## Round 4
- Text measuring no longer uses FreeType at all (the console's build gave no usable metrics, so labels were not
  centred and headings/badges were skipped). Widths now come from tables with the real advances of
  Gontserrat-Regular at 27 px and 42 px (`TextWidth`, `TextWidthPx`, `ShortenToWidthPx` in ui_draw.cpp).
  The "font must be non-null" early exits in DrawPill / DrawHeading / DrawTopBar / DrawStatusLine were removed.
- Game Plugins list and grid: "DEFAULT STATE" column is now "USE NUMBER" -
  DEFAULT (enabled in [default]), or "N GAMES" / "1 GAME" (installed games that enable it in their own section).
  See `CountPluginGames` in plugins.cpp.

## Round 5
- Removed the "JBC OK / JBC OFF" badge from the top bar (JBC failures are still reported in the scan status text).
- GET > File Browser redesigned (`DrawGetBrowser` in get.cpp): counts and "N selected" badges, path bar that keeps the
  end of long paths, folder/plugin/file icons, file SIZE column, FOLDER/PLUGIN/IMAGE/FILE badges (+PNG when a plugin has
  artwork), real checkboxes for plugin files, scrollbar, empty-folder state, clearer dialogs.
- "MOVE PLUGINS" renamed to "IMPORT ALL PLUGINS" (it copies the plugins, it never moved them).
- Going back (CIRCLE) puts the cursor on the folder you just left; folder sorting uses qsort instead of the O(n^2) swap loop.

## Round 6: Internet catalog opens fast
- Opening GET > Internet no longer blocks the screen. It shows a loading screen at once, then loads the list.
- The catalog list is saved to /data/PluginManager/online_manifest.json and reused for 15 minutes (SQUARE forces a refresh),
  so reopening the Internet view is instant.
- Images are no longer all downloaded/decoded up front (every artwork + both QR codes of every plugin). They load one
  per frame, only for what is on screen (highlighted item first); QR codes only when a plugin is selected. Tiles show
  "LOADING..." until their picture arrives. Nothing is loaded while a direction is auto-repeating.
- One HTTPS connection is kept alive and reused between requests (previously every image paid a full TLS handshake);
  if the reused connection turns out to be dead, the request is retried once on a fresh connection.
- Failure screen with "X: try again / O: back" instead of an empty catalog.
