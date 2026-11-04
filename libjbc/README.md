# libjbc build dependency

goldenPluginE uses the `jbc_get_cred`, `jbc_jailbreak_cred`, and `jbc_set_cred`
API from the PS4 `libjbc` project to request the filesystem access required by
the game scanner.

Upstream source repositories:

- https://github.com/sleirsgoevy/ps4-libjbc
- https://github.com/bucanero/ps4-libjbc (fallback)

The upstream implementation is fetched on the build machine by
`build-libjbc.sh`; its source is not vendored in this archive. The script
compiles the upstream `kernelrw.c` and `jailbreak.c` translation units, then
creates `libjbc.a`. `utils.c` is not needed for the three APIs used by this
application.

Requirements:

- Linux build host with `git`
- OpenOrbis PS4 toolchain installed
- `OO_PS4_TOOLCHAIN` pointing to the toolchain root
- `clang` and `ar`/`llvm-ar` available in the toolchain or `PATH`

From the project root, run `make`. The Makefile builds this archive as a
prerequisite before linking the app. To manually build it first:

```sh
./libjbc/build-libjbc.sh "$OO_PS4_TOOLCHAIN" linux
```

The bootstrap wrapper returns `0` on success, `-1` if reading the current
credentials fails, `-2` if preparing the credentials fails, and `-3` if applying
them fails. The application logs this result and continues without assuming
that protected filesystem paths are accessible.

This source-only project archive has not been built or tested on a PS4 in this
environment. The actual build and runtime result must be verified with the
intended OpenOrbis toolchain and target console. Review and retain the upstream
project's applicable license and attribution when redistributing builds.
