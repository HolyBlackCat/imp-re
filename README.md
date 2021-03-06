### imp-re

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
  * A Boost.Preprocess ripoff.
  * A tiny JSON parser.
  * . . .

Licensed under [ZLIB LICENSE](LICENSE.md).

---

Currently I'm targeting Clang 10.0.0 (with GCC 10's libstdc++; Windows and Linux). GCC 10 should work with minor adjustments. MSVC support would require significant changes, and a new built system.
