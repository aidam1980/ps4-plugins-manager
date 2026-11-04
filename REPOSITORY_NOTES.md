# Repository maintenance notes

- OpenOrbis SDK/toolchain files are intentionally not bundled. See
  `OPENORBIS_SETUP.md`.
- No project-wide open-source license has been added. Do not assume the source
  is licensed for redistribution or reuse until the project owner chooses and
  adds a license.
- `plugins.ini` and `payloads.ini` runtime/user configuration files are not
  included. This avoids publishing machine-specific plugin selections and
  payload paths. Create or manage them on the console as needed.
- The old `license.txt` in the supplied archive referred to an unrelated
  Stardew Valley mod and was removed rather than republished.
