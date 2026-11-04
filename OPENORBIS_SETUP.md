# OpenOrbis Toolchain setup

This repository contains the Plugins Manager application source code. It does
**not** bundle OpenOrbis SDK headers, libraries, linker files, or host
executables. Each developer must install the OpenOrbis PS4 Toolchain separately
and comply with its own license and distribution terms.

## Required items from OpenOrbis

Set `OO_PS4_TOOLCHAIN` to the root of the installed toolchain. The build uses:

| Toolchain path | Purpose |
|---|---|
| `include/` | PS4 system headers |
| `include/c++/v1/` | C++ standard library headers |
| `lib/` | PS4 system and C/C++ libraries |
| `lib/crt1.o` | PS4 program startup object |
| `link.x` | PS4 linker script |
| `bin/<host>/clang` | C compiler |
| `bin/<host>/clang++` | C++ compiler |
| `bin/<host>/ld.lld` | Linker |
| `bin/<host>/PkgTool.Core` | SFO and PKG operations |
| `bin/<host>/create-gp4` | GP4 project generation |
| `bin/<host>/create-fself` | FSELF/EBOOT generation |

`<host>` is `linux`, `macos`, or `windows`, according to the host tools
provided by the OpenOrbis release you installed. On Windows, use the Windows
`.exe` versions from the MSYS2/Git Bash environment.

The project keeps its shared `graphics.cpp` implementation in `_common/`, so
it does not need to copy `samples/_common/graphics.cpp` from OpenOrbis.

## Configure the toolchain path

### Linux

```bash
export OO_PS4_TOOLCHAIN="$HOME/PS4Dev/OpenOrbis/PS4Toolchain"
```

### Windows (MSYS2 / Git Bash)

Use an MSYS-style path:

```bash
export OO_PS4_TOOLCHAIN="/c/PS4Dev/OpenOrbis/PS4Toolchain"
```

Replace the example path with the actual installation location.

## Build

From the repository root:

```bash
make clean
make
```

The first build fetches the upstream `ps4-libjbc` source using Git and builds
`libjbc.a` locally. This requires Git and an internet connection. The fetched
source and generated build files are not committed to this repository.

## Verify the installation

Before building, check that the required paths exist:

```bash
test -d "$OO_PS4_TOOLCHAIN/include"
test -d "$OO_PS4_TOOLCHAIN/lib"
test -f "$OO_PS4_TOOLCHAIN/lib/crt1.o"
test -f "$OO_PS4_TOOLCHAIN/link.x"
```

Then check the host-specific executables under `bin/linux`, `bin/macos`, or
`bin/windows`.

## Important

- This is a PS4 homebrew project. Building on Windows creates a PS4 package,
  not a native Windows application.
- Use an OpenOrbis release whose directory layout and executable names match
  the paths above.
- Windows builds require GNU Make and a Bash-compatible shell (for example,
  MSYS2 or Git Bash); plain Command Prompt and PowerShell are not supported by
  this Makefile.
- The project has not been compiled or tested on Windows as part of preparing
  this source-only repository.
