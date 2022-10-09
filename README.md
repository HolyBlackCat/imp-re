## imp-re

A collection of various small libraries, made for personal use. Some of them are intended for game development, some are general-purpose.

Features:

* Convenient wrappers for SDL2, OpenGL, OpenAL.
* A simple 2D renderer.
* A macro-based reflection system for performing automatic [de]serialization, built on top of custom IO streams.
* Some fancy macros that improve the C++ syntax.

* Various bits and pieces:

  * A math library.
  * Metaprogramming utilities.
  * Scope guards.
  * A Boost.Preprocessor ripoff.
  * A tiny JSON parser.
  * . . .

Licensed under [ZLIB LICENSE](LICENSE.md).

### Building

Currently I'm targeting Clang 15 (with GCC 12's libstdc++; Windows and Linux). GCC 12 should work with minor adjustments. MSVC support would require some code changes, and a new build system.

#### Environment — Linux

Install Clang, GCC (for its libstdc++), `lld`, `make`, `cmake`, `zip`, `unzip`, `tar`, `zstd`, `pkg-config`, `rsync`, `patchelf`, `ldd`, `bc`.

Install dependencies for building SDL2. See their manual for the full list. Or, if you're using the current Ubuntu LTS, the necessary packages are also listed in `project.mk`.

#### Environment — Linux-to-Windows cross-compilation

Install the same tools as for native Linux builds.

Install [Quasi-MSYS2](https://github.com/HolyBlackCat/quasi-msys2), and do `make install _gcc _gdb _ntldd`.

#### Environment — Windows

Install [MSYS2](https://www.msys2.org/), and do `pacman -S bc diffutils git make mingw-w64-x86_64-clang mingw-w64-x86_64-cmake mingw-w64-x86_64-imagemagick mingw-w64-x86_64-lld mingw-w64-x86_64-ntldd ninja rsync tar unzip wget zip zstd`.

#### Invoking `make`

After configuring the environment, running `make` will build and run the app.

Do not add `-jN`, the number of threads is guessed automatically. Pass `JOBS=N` to override the number of threads. Pass `JOBS=` to disable the thread count override; then `-jN` will work normally.

Add `MODE=??` to override the build mode (`release`, `debug`, etc). See `project.mk` for a full list, or pass an invalid mode to print the available modes.

Run `make remember-mode MODE=??` to set the default mode.

The full list of commands:

* Building and running:
  * `make` or `make run-default` — build and run. You can use a project name instead of `default`, use tab completion for the current list.
  * `make run-old-default` — run without rebuilding. You might want to run `make sync-libs-and-assets` before this to sync the assets.
  * `make build-default` — build but don't run. You can use `all` or a project name instead of `default`, use tab completion for the current list.
* Misc:
  * `make remember-mode MODE=??` — set the default `MODE` and generate the debug configuration for VSC for it.
  * `make commands` — generate `compile_commands.json`. Respects the current `MODE`.
* Packaging:
  * `make dist` — package the binaries.
  * `make repeat-build-number` — decrement the build number for the next `make dist` by one.
  * `make dist-deps` — package the dependency sources into a single archive, like the one initially used to download them.
* Cleaning:
  * `make clean[-this-os[-this-mode]][-including-libs]` — Clean. By default we clean for all OSes and all build modes, use `-this-os` and `-this-mode` to constrain that. By default we don't clean built dependencies, use `-including-libs` to clean them as well.
  * `make clean-everything` — clean as much as possible (same as `make clean-including-libs`, plus more).
  * `make prepare-for-storage` — prepare the sources for archiving. Same as `make clean-everything`, plus archive library sources (like `make dist-deps`) and delete the originals.
* Manually building dependencies: (normally not needed)
  * `make libs` — build all dependencies.
  * `make lib-??` — build a dependency (use tab completion for a list).
  * `make clean-libs` — clean all dependencies.
  * `make clean-lib-??` — clean a dependency.
