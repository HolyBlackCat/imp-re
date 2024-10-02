# --- Build modes ---

_win_subsystem :=

$(call NewMode,debug)
$(Mode)GLOBAL_COMMON_FLAGS := -g
$(Mode)GLOBAL_CXXFLAGS := -D_GLIBCXX_DEBUG

$(call NewMode,debug_soft)
$(Mode)GLOBAL_COMMON_FLAGS := -g
$(Mode)GLOBAL_CXXFLAGS := -D_GLIBCXX_ASSERTIONS

$(call NewMode,release)
$(Mode)GLOBAL_COMMON_FLAGS := -O3
$(Mode)GLOBAL_CXXFLAGS := -DNDEBUG
$(Mode)GLOBAL_LDFLAGS := -s
$(Mode)PROJ_COMMON_FLAGS := -flto
$(Mode)PROJ_CXXFLAGS := -DIMP_PLATFORM_FLAG_prod=1
$(Mode)_win_subsystem := -mwindows

$(call NewMode,profile)
$(Mode)GLOBAL_COMMON_FLAGS := -O3 -pg
$(Mode)GLOBAL_CXXFLAGS := -DNDEBUG
$(Mode)_win_subsystem := -mwindows

$(call NewMode,sanitize_address_ub)
$(Mode)GLOBAL_COMMON_FLAGS := -g -fsanitize=address -fsanitize=undefined
$(Mode)GLOBAL_CXXFLAGS := -D_GLIBCXX_DEBUG
$(Mode)PROJ_RUNTIME_ENV += LSAN_OPTIONS=suppressions=misc/leak_sanitizer_suppressions.txt
# Otherwise we get odr-violation error on `z_errmsg`. Not sure what's going on.
# I previously fixed this error on Fedora, but there the build process of ZLib was broken, using a wrong compiler. That isn't what happens now, though.
$(Mode)PROJ_RUNTIME_ENV += ASAN_OPTIONS=detect_odr_violation=1

DIST_NAME := $(APP)_$(TARGET_OS)_v1.*
ifneq ($(MODE),release)
DIST_NAME := $(DIST_NAME)_$(MODE)
endif

# --- Project config ---

PROJ_CXXFLAGS += -std=c++2b -pedantic-errors -Wall -Wextra -Wdeprecated -Wextra-semi -Wimplicit-fallthrough
PROJ_CXXFLAGS += -ftemplate-backtrace-limit=0 -fmacro-backtrace-limit=0
PROJ_CXXFLAGS += -includesrc/program/common_macros.h -includesrc/program/parachute.h
PROJ_CXXFLAGS += -Isrc
PROJ_CXXFLAGS += -Ideps/sub/box2cpp/include
PROJ_CXXFLAGS += -Ideps/sub/minimacros/include -DM_SHORT_MACROS
PROJ_CXXFLAGS += -DIMGUI_USER_CONFIG=\"third_party_connectors/imconfig.h\"# Custom ImGui config.

ifeq ($(TARGET_OS),windows)
PROJ_LDFLAGS += $(_win_subsystem)
endif

# The common PCH rules for all projects.
override _pch_rules := src/game/*->src/game/master.hpp

$(call Project,exe,imp-re)
$(call ProjectSetting,source_dirs,src)
$(call ProjectSetting,cxxflags,-DDOCTEST_CONFIG_DISABLE)
$(call ProjectSetting,pch,$(_pch_rules))
$(call ProjectSetting,libs,*)
$(call ProjectSetting,bad_lib_flags,-Dmain=%>>>-DIMP_ENTRY_POINT_OVERRIDE=%)

$(call Project,exe,tests)
$(call ProjectSetting,source_dirs,src)
$(call ProjectSetting,cxxflags,-DIMP_ENTRY_POINT_OVERRIDE=unused_main)
$(call ProjectSetting,pch,$(_pch_rules))
$(call ProjectSetting,libs,*)
$(call ProjectSetting,bad_lib_flags,-Dmain)
ifeq ($(APP),tests)
ALLOW_PCH := 0# Force disable PCH for tests, it doesn't make much sense there.
endif


# --- Codegen ---

_codegen_command = $(CXX) -std=c++23 -Wall -Wextra -pedantic-errors
override _codegen_dir := gen
override _codegen_list := math:src/utils/mat.h macros:src/macros/generated.h
override _codegen_target = $2: $(_codegen_dir)/make_$1.cpp ; \
	@$(run_without_buffering)$(MAKE) -f gen/Makefile _gen_dir=$(_codegen_dir) _gen_source_file=make_$1 _gen_target_file=$2 _gen_command=$(call quote,$$(_codegen_command)) --no-print-directory
$(foreach f,$(_codegen_list),$(eval $(call _codegen_target,$(word 1,$(subst :, ,$f)),$(word 2,$(subst :, ,$f)))))



# --- Dependencies ---

# Don't need anything on Windows.

# On Ubuntu 22.04, install following for SDL2 (from docs/README-linux.md)
# sudo apt-get install build-essential git make autoconf automake libtool \
	pkg-config cmake ninja-build gnome-desktop-testing libasound2-dev libpulse-dev \
	libaudio-dev libsndio-dev libsamplerate0-dev libx11-dev libxext-dev \
	libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libwayland-dev \
	libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
	libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev fcitx-libs-dev \
	libpipewire-0.3-dev libdecor-0-dev
# This list was last updated for SDL 2.26.4.
# `libjack-dev` was removed from the list, because it caused weird package conflicts on Ubuntu 22.04.

# On Arch/Manjaro, install following for SDL2 (from https://gitlab.archlinux.org/archlinux/packaging/packages/sdl2/-/blob/main/PKGBUILD)
# pacman -S --needed glibc libxext libxrender libx11 libgl libxcursor hidapi libusb alsa-lib mesa libpulse libxrandr libxinerama wayland libxkbcommon wayland-protocols ibus fcitx5 libxss cmake jack ninja pipewire libdecor
# This list was last updated for SDL 2.30.2.
# Not sure if those allow all features to be build, but since we're not going
#   to distribute Arch binaries anyway, it shouldn't matter.

# On Fedora 38, install following for SDL2 (from docs/README-linux.md)
# sudo dnf install gcc git-core make cmake autoconf automake libtool \
    alsa-lib-devel pulseaudio-libs-devel nas-devel pipewire-devel \
    libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel \
    libXi-devel libXScrnSaver-devel dbus-devel ibus-devel fcitx-devel \
    systemd-devel mesa-libGL-devel libxkbcommon-devel mesa-libGLES-devel \
    mesa-libEGL-devel vulkan-devel wayland-devel wayland-protocols-devel \
    libdrm-devel mesa-libgbm-devel libusb1-devel libdecor-devel \
    libsamplerate-devel pipewire-jack-audio-connection-kit-devel
# `libusb-devel` was changed to `libusb1-devel` in this list, because there's no package `libusb-devel` in Fedora 38.
# This list was last updated for SDL 2.26.4.


# --- Libraries ---

DIST_DEPS_ARCHIVE := https://github.com/HolyBlackCat/imp-re/releases/download/deps-sources/deps_v7.zip

_win_is_x32 :=
_win_sdl2_arch := $(if $(_win_is_x32),i686-w64-mingw32,x86_64-w64-mingw32)

# Disable unnecessary stuff.

_openal_flags := -DALSOFT_EXAMPLES=FALSE
# Disable helper executables. Otherwise Windows builds fails because of missing Qt.
_openal_flags += -DALSOFT_UTILS=FALSE
# Enable SDL2 backend.
_openal_flags += -DALSOFT_REQUIRE_SDL2=TRUE -DALSOFT_BACKEND_SDL2=TRUE
# We used to disable other backends here, but it seems our CMake isolation works well enough to make this unnecessary.

ifeq ($(TARGET_OS),windows)
# They have an uname check that tells you to use a special makefile for Windows. Seems to be pointless though.
_zlib_env_vars += uname=linux
endif

# When you update this, check if they added installation rules for headers.
# To generate the new archive filename when updating (commit hash and date), you can use the comment at the beginning of our `box2c.hpp`.
$(call Library,box2c,box2c-1d7d1cf-2024-05-30.zip)
  $(call LibrarySetting,cmake_flags,-DBOX2D_SAMPLES:BOOL=OFF -DBOX2D_UNIT_TESTS:BOOL=OFF)
  $(call LibrarySetting,build_system,box2c)
override buildsystem-box2c =\
	$(call, ### Forward to CMake.)\
	$(buildsystem-cmake)\
	$(call, ### Install headers.)\
	$(call safe_shell_exec,cp -rT $(call quote,$(__source_dir)/include) $(call quote,$(__install_dir)/include))

# Delaunay triangulation library.
# It supports pre-baking the algorithms into a library, but that requires setting a global macro, which we don't have a convenient way of doing yet.
# So instead we use it in header-only mode...
# This also lets us move their headers to a `CDT` subdirectory, otherwise they have too much junk at the top level `include/`.
$(call Library,cdt,CDT-1.4.1+46f1ce1.zip)
  $(call LibrarySetting,build_system,copy_files)
  $(call LibrarySetting,copy_files,CDT/include/*->include/CDT)

$(call Library,cglfl,cglfl-74b2fcf.zip)
  $(call LibrarySetting,build_system,cglfl)
override buildsystem-cglfl = \
	$(call log_now,[Library] >>> Building generator...)\
	$(call safe_shell_exec,make CXX=$(call quote,$(HOST_CXX)) HOST_OS=$(if $(filter windows,$(HOST_OS)),windows,linux) -C $(call quote,$(__source_dir)) >>$(call quote,$(__log_path)))\
	$(call log_now,[Library] >>> Generating...)\
	$(call, ### Copy now, before the extra stuff is generated in `include`. We don't want it there, because)\
	$(call, ### we manually copy it later, changing the directory structure in a way that lets our)\
	$(call, ### include dir flag auto-detection do the right thing.)\
	$(call, ### This directory structure change prevents us from switching GL versions simply by changing the include flags though.)\
	$(call, ### This isn't a big deal, we just need to rebuild this dep. That is, in addition to all user code, as usual.)\
	$(call safe_shell_exec,cp -rT $(call quote,$(__source_dir)/include) $(call quote,$(__install_dir)/include))\
	$(call, ### Popular GL flavors: `gl2.1`, `gl3.2 core`, `gles2.0`)\
	$(call, ### Rebuild the library and all user code after switching flavors)\
	$(call safe_shell_exec,(cd $(call quote,$(__source_dir)) && ./cglfl_generate$(HOST_EXT_exe) gl3.2 core) >>$(call quote,$(__log_path)))\
	$(call safe_shell_exec,mv $(call quote,$(__source_dir)/include)/*/cglfl_generated $(call quote,$(__install_dir)/include/))\
	$(call, ### Move the executable away into the build dir)\
	$(call safe_shell_exec,mv $(call quote,$(__source_dir)/cglfl_generate$(HOST_EXT_exe)) $(call quote,$(__build_dir)))\
	$(call, ### Lastly, build the static library)\
	$(call log_now,[Library] >>> Building library...)\
	$(call safe_shell_exec,$(call language_command-cpp,$(__source_dir)/src/cglfl.cpp,$(__build_dir)/cglfl.o,,-I$(__install_dir)/include >>$(call quote,$(__log_path))))\
	$(call safe_shell_exec,mkdir $(call quote,$(__install_dir)/lib))\
	$(call safe_shell_exec,$(call MAKE_STATIC_LIB,$(__install_dir)/lib/$(PREFIX_static)cglfl$(EXT_static),$(call quote,$(__build_dir)/cglfl.o)) >>$(call quote,$(__log_path)))\
	$(call log_now,[Library] >>> Cleaning up...)

$(call Library,doctest,doctest-2.4.11.tar.gz)
  $(call LibrarySetting,cmake_flags,-DDOCTEST_WITH_TESTS:BOOL=OFF)# Tests don't compile because of their `-Werror`. Last tested on doctest-2.4.11, Clang 16.0.1.

$(call Library,double-conversion,double-conversion-3.3.0.tar.gz)

$(call Library,enkits,enkiTS-686d0ec-2024-05-29.zip)
  $(call LibrarySetting,cmake_flags,-DENKITS_INSTALL=ON -DENKITS_BUILD_SHARED=ON -DENKITS_BUILD_EXAMPLES=OFF)

$(call Library,fmt,fmt-11.0.1.zip)
  $(call LibrarySetting,cmake_flags,-DFMT_TEST=OFF)

ifeq ($(TARGET_OS),emscripten)
$(call LibraryStub,freetype,-sUSE_FREETYPE=1)
else
$(call Library,freetype,freetype-2.13.2.tar.gz)
  $(call LibrarySetting,deps,zlib)
endif

$(call Library,imgui,imgui-1.90.9.tar.gz)
  $(call LibrarySetting,build_system,imgui)
  # `stb` is needed because we delete ImGui's own copy of it, and tell it to use an external one.
  # `freetype` is needed because we enable it in `imconfig.h`.
  # `sdl2` is needed for the respective backend.
  # `fmt` isn't their dependency, but our `imconfig.h` needs it, because it includes `program/errors.h`.
  # `cglfl` isn't their dependency, but we make their GL backend files include it.
  $(call LibrarySetting,deps,stb freetype sdl2 fmt cglfl)
override buildsystem-imgui = \
	$(call log_now,[Library] >>> Copying files...)\
	$(call safe_shell_exec,mkdir $(call quote,$(__install_dir)/include) $(call quote,$(__install_dir)/lib))\
	$(foreach x,./* misc/cpp/* misc/freetype/* backends/imgui_impl_sdl2 backends/imgui_impl_opengl2 backends/imgui_impl_opengl3,\
		$(call safe_shell_exec,cp $(call quote,$(__source_dir))/$x.h $(call quote,$(__install_dir)/include/))\
		$(call safe_shell_exec,cp $(call quote,$(__source_dir))/$x.cpp $(call quote,$(__build_dir)))\
	) \
	$(call log_now,[Library] >>> Patching...)\
	$(call, ### Would destroy `imconfig.h` here, since we have a custom one, but ImGui still includes it in addition to `IMGUI_USER_CONFIG`. Strange.)\
	$(call, ### Patch `imgui_freetype.cpp` to silence Clang errors.)\
	$(call safe_shell_exec,awk '/#pragma GCC diagnostic push/ {print "#ifdef __clang__\n#pragma clang diagnostic ignored \"-Wunknown-warning-option\"\n#endif"} {print}' $(call quote,$(__build_dir)/imgui_freetype.cpp) >$(call quote,$(__build_dir)/tmp.cpp))\
	$(call safe_shell_exec,mv $(call quote,$(__build_dir)/tmp.cpp) $(call quote,$(__build_dir)/imgui_freetype.cpp))\
	$(call, ### Duplicate `imgui_freetype.h` next to the `.cpp` files, since ImGui looks for it there.)\
	$(call safe_shell_exec,mkdir -p $(call quote,$(__build_dir)/misc/freetype))\
	$(call safe_shell_exec,cp $(call quote,$(__install_dir)/include/imgui_freetype.h) $(call quote,$(__build_dir)/misc/freetype))\
	$(call, ### Destroy their STB copy, we have our own. Except for `imstb_textedit.h`, since theirs is slightly patched.)\
	$(call safe_shell_exec,rm $(call quote,$(__install_dir)/include)/imstb_rectpack.h $(call quote,$(__install_dir)/include)/imstb_truetype.h)\
	$(call, ### Wrap GL2 backend in version macro checks)\
	$(call, ### Note, can't use `echo -e` + `\n` for some reason. The flag is necessary on Windows, but is printed as text on Linux. Strange!)\
	$(call safe_shell_exec,(echo '#include <cglfl/cglfl.hpp>'; echo '#if CGLFL_GL_MAJOR == 2 && !defined(CGLFL_GL_API_gles)') >$(call quote,$(__build_dir)/tmp.cpp))\
	$(call safe_shell_exec,cat $(call quote,$(__build_dir)/imgui_impl_opengl2.cpp) >>$(call quote,$(__build_dir)/tmp.cpp))\
	$(call safe_shell_exec,echo '#endif' >>$(call quote,$(__build_dir)/tmp.cpp))\
	$(call safe_shell_exec,mv $(call quote,$(__build_dir)/tmp.cpp) $(call quote,$(__build_dir)/imgui_impl_opengl2.cpp))\
	$(call, ### Wrap GL3 backend in version macro checks)\
	$(call safe_shell_exec,(echo '#include <cglfl/cglfl.hpp>'; echo '#if CGLFL_GL_MAJOR > 2 || defined(CGLFL_GL_API_gles)') >$(call quote,$(__build_dir)/tmp.cpp))\
	$(call safe_shell_exec,cat $(call quote,$(__build_dir)/imgui_impl_opengl3.cpp) >>$(call quote,$(__build_dir)/tmp.cpp))\
	$(call safe_shell_exec,echo '#endif' >>$(call quote,$(__build_dir)/tmp.cpp))\
	$(call safe_shell_exec,mv $(call quote,$(__build_dir)/tmp.cpp) $(call quote,$(__build_dir)/imgui_impl_opengl3.cpp))\
	$(call, ### Build a static library)\
	$(call log_now,[Library] >>> Building...)\
	$(call var,__bs_sources := $(wildcard $(__build_dir)/*.cpp))\
	$(foreach x,$(__bs_sources),\
		$(call safe_shell_exec,$(call language_command-cpp,$x,$(x:.cpp=.o),,\
			-std=c++20 \
			-DIMGUI_USER_CONFIG=\"third_party_connectors/imconfig.h\"\
			-I$(call quote,$(__build_dir))\
			-I$(call quote,src)\
			-I$(call quote,$(__install_dir)/include)\
			$(call, ### Add flags for libfmt, freetype, and other deps. See above for explanation.)\
			$(call lib_cflags,$(__libsetting_deps_$(__lib_name)))\
			>>$(call quote,$(__log_path))\
		))\
	)\
	$(call safe_shell_exec,$(call MAKE_STATIC_LIB,$(__install_dir)/lib/$(PREFIX_static)imgui$(EXT_static),$(__bs_sources:.cpp=.o)) >>$(call quote,$(__log_path)))\

ifeq ($(TARGET_OS),emscripten)
$(call LibraryStub,ogg,-sUSE_OGG=1)
else
$(call Library,ogg,libogg-1.3.5.tar.gz) # Only serves as a dependency for `libvorbis`.
  # When built with CMake on MinGW, ogg/vorbis can't decide whether to prefix the libraries with `lib` or not.
  # The resulting executable doesn't find libraries because of this inconsistency.
  $(call LibrarySetting,build_system,configure_make)
endif

$(call Library,openal-soft,openal-soft-1.23.1.tar.gz)
  $(call LibrarySetting,deps,sdl2 zlib)# We want SDL2 as a backend. It's unclear what Zlib adds, we give it just because.
  $(call LibrarySetting,cmake_flags,$(_openal_flags))
ifneq ($(filter -D_GLIBCXX_DEBUG,$(GLOBAL_CXXFLAGS)),)
  $(call LibrarySetting,cxxflags,-U_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS)# The debug mode causes weird compilation errors.
endif

$(call Library,phmap,parallel-hashmap-1.3.12.tar.gz)
  $(call LibrarySetting,cmake_flags,-DPHMAP_BUILD_TESTS=OFF -DPHMAP_BUILD_EXAMPLES=OFF)# Otherwise it downloads GTest, which is nonsense.

ifeq ($(TARGET_OS),emscripten)
$(call LibraryStub,sdl2,-sUSE_SDL=2)
else ifeq ($(TARGET_OS),windows)
$(call Library,sdl2,SDL2-devel-2.30.5-mingw.tar.gz)
  $(call LibrarySetting,build_system,copy_files)
  $(call LibrarySetting,copy_files,$(_win_sdl2_arch)/*->.)
$(call Library,sdl2_net,SDL2_net-devel-2.2.0-mingw.tar.gz)
  $(call LibrarySetting,build_system,copy_files)
  $(call LibrarySetting,copy_files,$(_win_sdl2_arch)/*->.)
else
$(call Library,sdl2,SDL2-2.30.5.tar.gz)
  # Allow SDL to see system packages. If we were using `configure+make`, we'd need `configure_vars = env -uPKG_CONFIG_PATH -uPKG_CONFIG_LIBDIR` instead.
  $(call LibrarySetting,cmake_flags,-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=ON)
  $(call LibrarySetting,common_flags,-fno-sanitize=address -fno-sanitize=undefined)# ASAN/UBSAN cause linker errors in Linux, when making `libSDL2.so`. `-DSDL_ASAN=ON` doesn't help.
$(call Library,sdl2_net,SDL2_net-2.2.0.tar.gz)
  $(call LibrarySetting,deps,sdl2)
  $(call LibrarySetting,common_flags,-fno-sanitize=address -fno-sanitize=undefined)# See above.
endif

$(call Library,stb,stb-013ac3b-2024-05-31.zip)
  $(call LibrarySetting,build_system,copy_files)
  # Out of those, `rectpack` is used both by us and ImGui.
  # There's also `textedit`, which ImGui uses and we don't but we let ImGui keep its version, since it's slightly patched.
  $(call LibrarySetting,copy_files,stb_image.h->include stb_image_write.h->include stb_rect_pack.h->include)

ifeq ($(TARGET_OS),emscripten)
$(call LibraryStub,vorbis,-sUSE_VORBIS=1)
else
$(call Library,vorbis,libvorbis-1.3.7.tar.gz)
  $(call LibrarySetting,deps,ogg)
  # See ogg for why we use configure+make.
  $(call LibrarySetting,build_system,configure_make)
endif

ifeq ($(TARGET_OS),emscripten)
$(call LibraryStub,zlib,-sUSE_ZLIB=1)
else
$(call Library,zlib,zlib-1.3.1.tar.gz)
  # CMake support in ZLib is jank. On MinGW it builds `libzlib.dll`, but pkg-config says `-lz`. Last checked on 1.2.12.
  $(call LibrarySetting,build_system,configure_make)
  # Need to set `cc`, otherwise their makefile uses the executable named `cc` to link, which doesn't support `-fuse-ld=lld-N`, it seems. Last tested on 1.2.12.
  $(call LibrarySetting,configure_vars,$(_zlib_env_vars))
endif
