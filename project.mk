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

DIST_NAME := $(APP)_$(TARGET_OS)_v1.*
ifneq ($(MODE),release)
DIST_NAME := $(DIST_NAME)_$(MODE)
endif

# --- Project config ---

PROJ_CXXFLAGS += -std=c++2b -pedantic-errors -Wall -Wextra -Wdeprecated -Wextra-semi
PROJ_CXXFLAGS += -ftemplate-backtrace-limit=0 -fmacro-backtrace-limit=0
PROJ_CXXFLAGS += -includesrc/program/common_macros.h -includesrc/program/parachute.h
PROJ_CXXFLAGS += -Isrc -Ilib/include
PROJ_CXXFLAGS += -DIMGUI_USER_CONFIG=\"third_party_connectors/imconfig.h\"# Custom ImGui config.
PROJ_CXXFLAGS += -DFMT_DEPRECATED_OSTREAM# See issue: https://github.com/fmtlib/fmt/issues/3088

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

_codegen_command = $(CXX) -std=c++20 -Wall -Wextra -pedantic-errors
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

DIST_DEPS_ARCHIVE := https://github.com/HolyBlackCat/imp-re/releases/download/deps-sources/deps_v4.zip

_win_is_x32 :=
_win_sdl2_arch := $(if $(_win_is_x32),i686-w64-mingw32,x86_64-w64-mingw32)

# Disable unnecessary stuff.
# I'd also pass `-DBUILD_CLSOCKET:BOOL=OFF -DBUILD_CPU_DEMOS:BOOL=OFF -DBUILD_ENET:BOOL=OFF`, but CMake considers them unused,
# even though they appear in the cache even if not specified. Maybe they're related to the things we've removed from the Bullet distribution?
_bullet_flags := -DBUILD_BULLET2_DEMOS:BOOL=OFF -DBUILD_EXTRAS:BOOL=OFF -DBUILD_OPENGL3_DEMOS:BOOL=OFF -DBUILD_UNIT_TESTS:BOOL=OFF
# Use doubles instead of floats.
_bullet_flags += -DUSE_DOUBLE_PRECISION:BOOL=ON
# Disable shared libraries. This should be the default behavior (with the flags above), but we also set it for a good measure.
_bullet_flags += -DBUILD_SHARED_LIBS:BOOL=OFF
# This defaults to off if the makefile flavor is not exactly "Unix Makefiles", which is silly.
# That used to cause 'make install' to not install anything useful.
_bullet_flags += -DINSTALL_LIBS:BOOL=ON

_openal_flags := -DALSOFT_EXAMPLES=FALSE
# Enable SDL2 backend.
_openal_flags += -DALSOFT_REQUIRE_SDL2=TRUE -DALSOFT_BACKEND_SDL2=TRUE
# We used to disable other backends here, but it seems our CMake isolation works well enough to make this unnecessary.

ifeq ($(TARGET_OS),windows)
# They have an uname check that tells you to use a special makefile for Windows. Seems to be pointless though.
_zlib_env_vars += uname=linux
endif

$(call Library,box2d,box2d-2.4.1.tar.gz)
  $(call LibrarySetting,cmake_flags,-DBOX2D_BUILD_UNIT_TESTS:BOOL=OFF -DBOX2D_BUILD_TESTBED:BOOL=OFF)

$(call Library,bullet,bullet3-3.25_no-examples.tar.gz)
  # The `_no-examples` suffix on the archive indicates that following directories were removed from it: `./data`, and everything in `./examples` except `CommonInterfaces`.
  # This decreases the archive size from 170+ mb to 10+ mb.
  $(call LibrarySetting,cmake_flags,$(_bullet_flags))

$(call Library,doctest,doctest-2.4.11.tar.gz)
  $(call LibrarySetting,cmake_flags,-DDOCTEST_WITH_TESTS:BOOL=OFF)# Tests don't compile because of their `-Werror`. Last tested on doctest-2.4.11, Clang 16.0.1.

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
	$(call log_now,[Library] >>> Cleaning up...)\

$(call Library,double-conversion,double-conversion-3.2.1.tar.gz)

$(call Library,fmt,fmt-9.1.0.zip)
  $(call LibrarySetting,cmake_flags,-DFMT_TEST=OFF)

$(call Library,freetype,freetype-2.13.0.tar.gz)
  $(call LibrarySetting,deps,zlib)

$(call Library,imgui,imgui-1.89.4.tar.gz)
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

$(call Library,ogg,libogg-1.3.5.tar.gz) # Only serves as a dependency for `libvorbis`.
  # When built with CMake on MinGW, ogg/vorbis can't decide whether to prefix the libraries with `lib` or not.
  # The resulting executable doesn't find libraries because of this inconsistency.
  $(call LibrarySetting,build_system,configure_make)

$(call Library,openal-soft,openal-soft-1.23.0.tar.gz)
  $(call LibrarySetting,deps,sdl2 zlib)# We want SDL2 as a backend. It's unclear what Zlib adds, we give it just because.
  $(call LibrarySetting,cmake_flags,$(_openal_flags))
ifneq ($(filter -D_GLIBCXX_DEBUG,$(GLOBAL_CXXFLAGS)),)
  $(call LibrarySetting,cxxflags,-U_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS)# The debug mode causes weird compilation errors.
endif

$(call Library,phmap,parallel-hashmap-1.3.8.tar.gz)
  $(call LibrarySetting,cmake_flags,-DPHMAP_BUILD_TESTS=OFF -DPHMAP_BUILD_EXAMPLES=OFF)# Otherwise it downloads GTest, which is nonsense.

ifeq ($(TARGET_OS),windows)
$(call Library,sdl2,SDL2-devel-2.26.4-mingw.tar.gz)
  $(call LibrarySetting,build_system,copy_files)
  $(call LibrarySetting,copy_files,$(_win_sdl2_arch)/*->.)
$(call Library,sdl2_net,SDL2_net-devel-2.2.0-mingw.tar.gz)
  $(call LibrarySetting,build_system,copy_files)
  $(call LibrarySetting,copy_files,$(_win_sdl2_arch)/*->.)
else
$(call Library,sdl2,SDL2-2.26.4.tar.gz)
  # Allow SDL to see system packages. If we were using `configure+make`, we'd need `configure_vars = env -uPKG_CONFIG_PATH -uPKG_CONFIG_LIBDIR` instead.
  $(call LibrarySetting,cmake_flags,-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=ON)
  $(call LibrarySetting,common_flags,-fno-sanitize=address -fno-sanitize=undefined)# ASAN/UBSAN cause linker errors in Linux, when making `libSDL2.so`. `-DSDL_ASAN=ON` doesn't help.
$(call Library,sdl2_net,SDL2_net-2.2.0.tar.gz)
  $(call LibrarySetting,deps,sdl2)
  $(call LibrarySetting,common_flags,-fno-sanitize=address -fno-sanitize=undefined)# See above.
endif

$(call Library,stb,stb-5736b15.zip)
  $(call LibrarySetting,build_system,copy_files)
  # Out of those, `rectpack` is used both by us and ImGui.
  # There's also `textedit`, which ImGui uses and we don't but we let ImGui keep its version, since it's slightly patched.
  $(call LibrarySetting,copy_files,stb_image.h->include stb_image_write.h->include stb_rect_pack.h->include)

$(call Library,vorbis,libvorbis-1.3.7.tar.gz)
  $(call LibrarySetting,deps,ogg)
  # See ogg for why we use configure+make.
  $(call LibrarySetting,build_system,configure_make)

$(call Library,zlib,zlib-1.2.13.tar.gz)
  # CMake support in ZLib is jank. On MinGW it builds `libzlib.dll`, but pkg-config says `-lz`. Last checked on 1.2.12.
  $(call LibrarySetting,build_system,configure_make)
  # Need to set `cc`, otherwise their makefile uses the executable named `cc` to link, which doesn't support `-fuse-ld=lld-N`, it seems. Last tested on 1.2.12.
  $(call LibrarySetting,configure_vars,$(_zlib_env_vars))
