# --- Configure Make ---

# `rR` disables builtin variables and flags.
# `rR` doesn't properly disable built-in variables. They only disappear for recipes, but still exist for initial makefile parse.
# We manually unset them, except for a few useful ones.
$(foreach x,$(filter-out .% MAKE% SHELL CURDIR,$(.VARIABLES)) MAKEINFO,$(if $(filter default,$(origin $x)),$(eval override undefine $x)))

# `-Otarget` groups the output by recipe (manual says it's the default behavior, which doesn't seem to be the case.)
MAKEFLAGS += -rR -Otarget

# Automatically parallelize.
JOBS := $(shell nproc)$(if $(filter-out 0,$(.SHELLSTATUS)),$(info [Warning] Unable to determine the number of cores.)1)
$(if $(filter 1,$(JOBS)),$(info [Warning] Building in a single thread.))
ifneq ($(JOBS),)
MAKEFLAGS += -j$(JOBS)
endif

# Prevent recursive invocations of Make from using our flags.
# This fixes some really obscure bugs.
unexport MAKEFLAGS

# Disable localized messages.
override LANG :=
export LANG

# Set the default target.
.DEFAULT_GOAL :=  run-current


# --- Define contants ---

override define lf :=
$(call)
$(call)
endef

override space := $(call) $(call)
override comma := ,

# The directory with the makefile.
override proj_dir := $(patsubst ./,.,$(dir $(firstword $(MAKEFILE_LIST))))

# A string of all single-letter Make flags, without spaces.
override single_letter_makeflags := $(filter-out -%,$(firstword $(MAKEFLAGS)))

# If non-empty, we're probably running a tab completion in a shell right now. Avoid raising errors.
override running_tab_completion := $(and $(findstring p,$(single_letter_makeflags)),$(findstring q,$(single_letter_makeflags)))

# Prepend this to a recipe line (without space) to run it unbuffered.
# Normally expands to `+`, but if `-p` or `-q` are used, expands to nothing (to avoid running the line when nothing else runs).
override run_without_buffering := $(if $(or $(findstring n,$(single_letter_makeflags)),$(findstring q,$(single_letter_makeflags))),,+)


# --- Define functions ---

# Used to create local variables in a safer way. E.g. `$(call var,x := 42)`.
override var = $(eval override $(subst $,$$$$,$1))

# Encloses $1 in single quotes, with proper escaping for the shell.
# If you makefile uses single quotes everywhere, a decent way to transition is to manually search and replace `'(\$(?:.|\(.*?\)))'` with `$(call quote,$1)`.
override quote = '$(subst ','"'"',$1)'

ifneq ($(findstring n,$(single_letter_makeflags)),)
# See below.
override safe_shell = $(info Would run shell command: $1)
override shell_status = $(info Would check command status: $1)
else ifeq ($(filter --trace,$(MAKEFLAGS)),)
# Same as `$(shell ...)`, but triggers a error on failure.
override safe_shell = $(shell $1)$(if $(filter-out 0,$(.SHELLSTATUS)),$(error Unable to execute `$1`, exit code $(.SHELLSTATUS)))
# Same as `$(shell ...)`, expands to the shell status code rather than the command output.
override shell_status = $(call,$(shell $1))$(.SHELLSTATUS)
else
# Same functions but with logging.
override safe_shell = $(info Shell command: $1)$(shell $1)$(if $(filter-out 0,$(.SHELLSTATUS)),$(error Unable to execute `$1`, exit code $(.SHELLSTATUS)))
override shell_status = $(info Shell command: $1)$(call,$(shell $1))$(.SHELLSTATUS)$(info Exit code: $(.SHELLSTATUS))
endif

# Same as `safe_shell`, but discards the output and expands to nothing.
override safe_shell_exec = $(call,$(call safe_shell,$1))

# Same as the built-in `wildcard`, but without the dumb caching issues and with more sanity checks.
# Make tends to cache the results of `wildcard`, and doesn't invalidate them when it should.
override safe_wildcard = $(foreach x,$(call safe_shell,echo $1),$(if $(filter 0,$(call shell_status,test -e $(call quote,$x))),$x))

# $1 is a directory. If it has a single subdirectory and nothing more, adds it to the path, recursively.
# I'm not sure why `safe_wildcard` is needed here. The built-in `wildcard` sometimes likes to return stale results from previous parameters, weird.
override most_nested = $(call most_nested_low,$1,$(filter-out $(most_nested_ignored_files),$(call safe_wildcard,$1/*)))
override most_nested_low = $(if $(filter 1,$(words $2)),$(call most_nested,$2),$1)
# `most_nested` ignores those filenames.
override most_nested_ignored_files = pax_global_header

# A recursive wildcard function.
# Source: https://stackoverflow.com/a/18258352/2752075
# Recursively searches a directory for all files matching a pattern.
# The first parameter is a directory, the second is a pattern.
# Example usage: SOURCES = $(call rwildcard, src, *.c *.cpp)
# This implementation differs from the original. It was changed to correctly handle directory names without trailing `/`, and several directories at once.
override rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

# Writes $1 to the output, immediately. Similar to $(info), but not delayed until the end of the recipe, when output grouping is used.
override log_now = $(if $(filter-out true,$(MAKE_TERMOUT)),$(file >$(MAKE_TERMOUT),$1),$(info $1))

# Like $(filter), but matches any part of a word, rather than the whole word. Doesn't accept `%`s.
override filter_substr = $(strip $(foreach x,$2,$(foreach y,$1,$(if $(strip $(findstring $y,$x)),$x))))

# $1 is a separator. $2 is a space-separated list of pairs, with $1 between the two elements. $3 is the target string.
# E.g. `$(call pairwise_subst,>>>,1>>>11 3>>>33 5>>>55,1 2 3 4 5 6)` returns `11 2 33 4 55 6`.
# You can also use `%` in both pair elements to replace with a pattern.
override pairwise_subst = $(if $2,$(call pairwise_subst_low,$1,$(subst $1, ,$(firstword $2)),$3,$(wordlist 2,$(words $2),$2)),$3)
override pairwise_subst_low = $(if $(filter-out 1 2,$(words $2)),$(error The separator can't appear more than once per element))$(call pairwise_subst,$1,$4,$(patsubst $(word 1,$2),$(word 2,$2),$3))

# Given a word, matches it against a set of rules. Returns the name of the first matching rule, or empty string on failure.
# $4 is the target word.
# $3 is a list of space-separated rules, where each rule is `<pattens>$1<name>`, where patterns are separated by $2 and can contain %.
# Example: `$(call find_first_match,->,;,a->letter 1;2;3->number,2)` returns `number`.
override find_first_match = $(firstword\
	$(if $(filter-out 1,$(words $4)),$(error Input must have exactly one word))\
	$(foreach x,$3,\
		$(if $(filter-out 2,$(words $(subst $1, ,$x))),$(error Each element must have exactly one separator))\
		$(if $(filter $(subst $2, ,$(firstword $(subst $1, ,$x))),$4),$(lastword $(subst $1, ,$x)))\
	)\
)


# --- Archive support ---

# The only reason to define this so early is to let the config modify those.

# Given archive filename $1, tries to determine the archive type. Returns one of `archive_types`, or empty on failure.
override archive_classify_filename = $(firstword $(foreach x,$(archive_types),$(if $(call archive_is-$x,$1),$x)))

# Given archive filenames $1, returns them without extensions.
# Essentially returns filename without extensions, and without the `lib` prefix. Can work with lists.
# It's recursive to be able to handle `.tar.gz`, and so on.
override strip_archive_extension = $(foreach x,$1,$(if $(call archive_classify_filename,$x),$(call strip_archive_extension,$(basename $x)),$(patsubst lib%,%,$x)))

archive_types :=

# Archive type definitions:

# `archive_is-<type>` - given filename $1, should expand to a non-empty string if it's an archive of this type.
# `archive_extract-<type>` - should expand to a command to extract archive $1 to directory $2.

archive_types += TAR
override archive_is-TAR = $(or $(findstring .tar.,$1),$(filter %.tar,$1))
override archive_extract-TAR = tar -xf $(call quote,$1) -C $(call quote,$2)

archive_types += ZIP
override archive_is-ZIP = $(filter %.zip,$1)
# `-o` allows us to overwrite files. Good for at least `make -B deps_src`.
override archive_extract-ZIP = unzip -oq $(call quote,$1) -d $(call quote,$2)


# --- Build system detection config ---

# It must be up here, to allow modifications from the configs.

# Modify this variable to tweak build system detection.
# Order matters, the first match is used.
# A space separated list of `<filename>-><buildsystem>`.
# We give preference to CMake, because it's easier to deal with.
# Downsides:
# * zlib - CMake support is jank. On MinGW it produces `libzlib.dll`, while pkg-config says to link `-lz`.
# * vorbis - On Windows, tries to link `ogg.dll` instead of `libogg.dll` for some reason. Use `configure_make` for it, and for ogg for a good measure.
buildsystem_detection := CMakeLists.txt->cmake configure->configure_make


# --- Language definitions ---

language_list :=

# Everything should be mostly self-explanatory.
# `language_outputs_deps` describes whether an extra `.d` file is created or not (don't define it if not).
# In `language_command`:
# $1 is the input file.
# $2 is the output file, or empty string if we don't care.
# $3 is the project name, or empty string if none.
# $4 is extra flags.
# $5 - if non-empty string, output dependencies as if by -MMD -MP, if this language supports it. You must also set `language_outputs_deps-??` to non-empty strin if you support it.

language_list += c
override language_name-c := C
override language_pattern-c := *.c
override language_command-c = $(strip $(CC) $(if $3,$(call pch_flag_for_source,$1,$3)) $4 $(if $5,-MMD -MP) -c $1 $(if $2,-o $2) $(if $3,$(call proj_libs_filtered_flags,cflags,$3)) $(combined_global_cflags) $(if $3,$(PROJ_COMMON_FLAGS) $(PROJ_CFLAGS) $(__projsetting_common_flags_$3) $(__projsetting_cflags_$3) $(call $(__projsetting_flags_func_$3),$1)))
override language_outputs_deps-c := y
override language_link-c = $(CC)
override language_pchflag-c := -xc-header

language_list += cpp
override language_name-cpp := C++
override language_pattern-cpp := *.cpp
override language_command-cpp = $(strip $(CXX) $(if $3,$(call pch_flag_for_source,$1,$3)) $4 $(if $5,-MMD -MP) -c $1 $(if $2,-o $2) $(if $3,$(call proj_libs_filtered_flags,cflags,$3)) $(combined_global_cxxflags) $(if $3,$(PROJ_COMMON_FLAGS) $(PROJ_CXXFLAGS) $(__projsetting_common_flags_$3) $(__projsetting_cxxflags_$3) $(call $(__projsetting_flags_func_$3),$1)))
override language_outputs_deps-cpp := y
override language_link-cpp = $(CXX)
override language_pchflag-cpp := -xc++-header

language_list += rc
override language_name-rc := Resource
override language_pattern-rc := *.rc
override language_command-rc = $(WINDRES) $(WINDRES_FLAGS) $4 -i $1 -o $2

# A helper pseudo-language to compile icons. We want it because we can't set make dependencies for .rc automatically.
language_list += ico
override language_name-ico := Icon
override language_pattern-ico := *.ico
override language_command-ico = echo $(call quote,"$1" ICON "$1") >$(call quote,$(2:.o=.rc)) && $(WINDRES) $(WINDRES_FLAGS) $4 -i $(call quote,$(2:.o=.rc)) -o $(call quote,$2)


# --- Define public config functions ---

# Only the ones starting with uppercase letters are actually public.

# List of all libraries.
override all_libs :=
# Defines a new library.
# $1 is a library name. $2 is the corresponding archive name.
override Library = \
	$(if $(filter-out 1,$(words $1)),$(error The library name must be a single word.))\
	$(if $(filter-out 1,$(words $2)),$(error The archive filename must be a single word.))\
	$(call var,all_libs += $1)\
	$(call var,__libsetting_archive_$(strip $1) := $(strip $2))

override all_lib_stubs :=
# Defines a new library stub. Building it is a no-op, it's just a collection of flags.
# $1 is a library name, $2 are common flags, $3 are cflags, $4 are ldflags.
override LibraryStub = \
	$(call var,all_lib_stubs += $1)\
	$(call var,__libsetting_is_stub_$(strip $1) := 1)\
	$(call var,__libsetting_stub_cflags_$(strip $1) := $(strip $2 $3))\
	$(call var,__libsetting_stub_ldflags_$(strip $1) := $(strip $2 $4))

# Known library setting names.
# `archive` isn't here, because it's set directly by `Library`.
override lib_setting_names := cflags cxxflags ldflags common_flags deps build_system cmake_flags configure_vars configure_flags copy_files bad_pkgconfig only_pkgconfig_files
# On success, assigns $2 to variable `__libsetting_$1_<lib>`. Otherwise causes an error.
# Settings are:
# * {c,cxx,ld,common_}flags - per-library flag customization.
# * deps - library dependencies, that this library will be allowed to see when building. A space-separated list of library names.
# * build_system - override build system detection. Can be: `cmake`, `configure_make`, etc. See `id_build_system` below for the full list.
# * cmake_flags - if CMake is used, those are passed to it. Probably should be a list of `-D<var>=<value>`.
# * configure_vars - if configure+make is used, this is prepended to `configure` and `make`. This should be a list of `<var>=<value>`, but you could use `/bin/env` there too.
# * configure_flags - if configure+make is used, this is passed to `./configure`.
# * copy_files - if `copy_files` build system is used, this must be specified to describe what files/dirs to copy.
#                Must be a space-separated list of `src->dst`, where `src` is relative to source and `dst` is relative to the install prefix. Both can be files or directories.
# * bad_pkgconfig - if not empty (or 0), destroy the pkg-config files for the library. This causes us to fall back to the automatic flag detection.
# * only_pkgconfig_files - if specified, use those pkgconfig files instead of all available ones. A space-separated list without extensions.
override LibrarySetting = \
	$(if $(filter-out $(lib_setting_names),$1)$(filter-out 1,$(words $1)),$(error Invalid library setting `$1`, expected one of: $(lib_setting_names)))\
	$(if $(filter 0,$(words $(all_libs))),$(error Must specify library settings after a library))\
	$(call var,__libsetting_$(strip $1)_$(lastword $(all_libs)) := $2)

# List of projects.
override proj_list :=
# Allowed project types.
override proj_kind_names := exe shared
override proj_kind_name-exe := Executable
override proj_kind_name-shared := Shared library

# $1 is the project kind, one of `proj_kind_names`.
# $2 is the project name, or a space-separated list of them.
# A list is not recommended, because ProjectSetting always applies only to the last library.
override Project = \
	$(if $(filter-out 1,$(words $1)),$(error Project name must be a single word))\
	$(if $(or $(filter-out $(proj_kind_names),$1),$(filter-out 1,$(words $1))),$(error Project kind must be one of: $(proj_kind_names)))\
	$(call var,proj_list += $2)\
	$(call var,__proj_kind_$(strip $2) := $(strip $1))\

override proj_setting_names := sources source_dirs cflags cxxflags ldflags common_flags flags_func pch deps libs bad_lib_flags lang runtime_env

# On success, assigns $2 to variable `__projsetting_$1_<lib>`. Otherwise causes an error.
# Settings are:
# * sources - individual source files.
# * source_dirs - directories to search for source files. The result is combined with `sources`.
# * cflags,cxxflags - compiler flags for C and CXX respectively.
# * ldflags - linker flags.
# * common_flags - those are added to both `{c,cxx}flags` and `ldflags`.
# * flags_func - a function name to determine extra per-file flags. The function is given the source filename as $1, and can return flags if it wants to.
# * pch - the PCH configuration. A space-separated list of `patterns->header`, where `header` is the PCH filename and `patters` is a `;`-separated list of source filenames, possibly containing `*`s.
# * deps - a space-separated list of projects that this project depends on.
# * libs - a space-separated list of libraries created with $(Library), or `*` to use all libraries.
# * bad_lib_flags - those flags are removed from the library flags (both cflags and ldflags). You can also use replacements here, in the form of `a>>>b`, which may contain `%`.
# * lang - either `c` or `cpp`. Sets the language for linking and PCH.
# * runtime_env - a space-separated list of environment variables (`env=value`) for running the resulting binary. Appended to `PROJ_RUNTIME_ENV`.
override ProjectSetting = \
	$(if $(filter-out $(proj_setting_names),$1)$(filter-out 1,$(words $1)),$(error Invalid project setting `$1`, expected one of: $(proj_setting_names)))\
	$(if $(filter 0,$(words $(proj_list))),$(error Must specify project settings after a project))\
	$(call var,__projsetting_$(strip $1)_$(lastword $(proj_list)) := $2)

override mode_list :=

# Creates a new build mode $1. You can use `Mode` to configure it, or directly check `MODE` and modify flags accordingly.
# Usage: `$(call NewMode,release)`
override NewMode = \
	$(if $(filter-out 1,$(words $1)),$(error Mode name must be a single word))\
	$(if $(filter generic,$1),$(error Mode name `generic` is reserved))\
	$(call var,mode_list += $1)

# Runs the rest of the line immediately, if the mode matches.
# Usage: `$(Mode)VAR := ...` (or `+=`, or any other line).
# The lack of space after `$(Mode)` is critical. Otherwise you get an error, regardless of the current mode.
override Mode = $(if $(strip $(filter-out $(lastword $(mode_list)),$(MODE))),override __unused =)


# --- Set default values before loading configs ---

# Detect target OS.
ifeq ($(TARGET_OS),)
ifneq ($(EMSCRIPTEN),)# Emmake sets this.
TARGET_OS := emscripten
else ifeq ($(OS),Windows_NT)# Quasi-MSYS2 sets this.
TARGET_OS := windows
else
TARGET_OS := linux
endif
endif

# Detect host OS.
# I had two options: `uname` and `uname -o`. The first prints `Linux` and `$(MSYSTEM)-some-junk`, and the second prints `GNU/Linux` and `Msys` on Linux and MSYS2 respectively.
# I don't want to parse MSYSTEM, so I decided to use `uname -o`.
ifneq ($(findstring Msys,$(call safe_shell,uname -o)),)
HOST_OS := windows
else
HOST_OS := linux
endif

# Configure output extensions/prefixes.
PREFIX_exe :=
PREFIX_shared := lib
PREFIX_static := lib
ifeq ($(TARGET_OS),windows)
EXT_exe := .exe
EXT_shared := .dll
EXT_static := .a
else ifeq ($(TARGET_OS),emscripten)
EXT_exe := .html
EXT_shared := .so
EXT_static := .a
else
EXT_exe :=
EXT_shared := .so
EXT_static := .a
endif
ifeq ($(HOST_OS),windows)
HOST_EXT_exe := .exe
else
HOST_EXT_exe :=
endif

# Check if the filename looks like a shared library.
# This is used together with `SHARED_LIB_DIR_IN_PREFIX` to find library files in compiled libraries. In a pinch, you could always return true.
ifeq ($(TARGET_OS),windows)
IS_SHARED_LIB_FILENAME = $(filter %$(EXT_shared),$1)
else
# Normally we only link against `foo.so.X`, but vorbis links against `foo.so.X.Y.Z` instead, so we accept all of them.
IS_SHARED_LIB_FILENAME = $(or $(filter %$(EXT_shared),$1),$(findstring $(EXT_shared).,$1))
endif

# Where in a prefix the shared libraries are located.
ifeq ($(TARGET_OS),windows)
SHARED_LIB_DIR_IN_PREFIX := bin
else
SHARED_LIB_DIR_IN_PREFIX := lib
endif

# Traditional variables:
export CC ?=
export CXX ?=
export CPP ?=
export LD ?=
export CFLAGS ?=
export CXXFLAGS ?=
export CPPFLAGS ?=
export LDFLAGS ?=

# LDD. We don't care about the Quasi-MSYS2's `win-ldd` wrapper since we can convert paths ourselves. We need it for the native Winwdows anyway.
# Also an optional program to preprocess the paths from LDD.
ifeq ($(TARGET_OS),windows)
LDD := ntldd -R
ifeq ($(HOST_OS),windows)
LDD_PREPROCESS_PATHS = cygpath -u $1
else
LDD_PREPROCESS_PATHS = realpath `winepath -u $1`
endif
else
LDD := ldd
LDD_PREPROCESS_PATHS =
endif

# Patchelf command, if necessary.
ifeq ($(TARGET_OS),windows)
PATCHELF :=
GLOBAL_LDFLAGS_RPATH :=
else
PATCHELF := patchelf --set-rpath '$$ORIGIN'
GLOBAL_LDFLAGS_RPATH := -Wl,-rpath='$$ORIGIN'
endif

# Windres settings:
WINDRES := windres
WINDRES_FLAGS := -O res

# This command produces static libraries. $1 is the resulting filename, $2 are the input object files.
# Only $2 needs to be quoted.
MAKE_STATIC_LIB = ar rvc $(call quote,$1) $2

# Prevent pkg-config from finding external packages.
# Note the stupid `-` signs. Fedora's stupid `pkg-config` is a stupid shell script that sets those to fallback values if they're unset OR EMPTY.
override PKG_CONFIG_PATH := -
export PKG_CONFIG_PATH
override PKG_CONFIG_LIBDIR := -
export PKG_CONFIG_LIBDIR

MODE :=# Build mode. Set to `generic` to not add any custom flags.
APP :=# Target project
ARGS :=# Flags for running the application.

COMMON_FLAGS :=# Used both when compiling and linking.
# The linker, e.g. `lld` or `lld-13`. Can be `AUTO` to guess LLD version, or empty to use the default one.
ifeq ($(TARGET_OS),emscripten)
LINKER :=
else
LINKER := AUTO
endif
ALLOW_PCH := 1# If 0 or empty, disable PCH.
CMAKE_GENERATOR := Ninja# CMake generator, not quoted. Optional.
CMAKE_BUILD_TOOL := ninja# CMake build tool, such as `make` or `ninja`. Optional. If specified, must match the generator.

# Used both when compiling and linking. Those are set automatically.
IMPLICIT_COMMON_FLAGS :=
ifneq ($(TARGET_OS),windows)
# Without this we can't build shared libraries.
# Also libfmt is known to produce static libs without this flag, meaning they can't later be linked into our shared libs.
IMPLICIT_COMMON_FLAGS += -fPIC
endif
ifneq ($(MAKE_TERMERR),)
# -Otarget messes with the colors, so we fix it here.
IMPLICIT_COMMON_FLAGS += -fdiagnostics-color=always
endif

# Flags applied to both projects and dependencies.
GLOBAL_COMMON_FLAGS :=# Combined with `{C,CXX,LD}FLAGS`.
GLOBAL_CFLAGS :=
GLOBAL_CXXFLAGS :=
GLOBAL_LDFLAGS :=
# Flags applied to all our projects, but not to the dependencies.
PROJ_COMMON_FLAGS :=# Combined with `{C,CXX,LD}FLAGS`.
PROJ_CFLAGS :=
PROJ_CXXFLAGS :=
PROJ_LDFLAGS :=
PROJ_RUNTIME_ENV :=# A space-separated list of environment variables (`env=value`), used when running the resulting binary.

# Host toolchain.
# `emmake` sets those, but we don't really care what they want the host compiler to be.
HOST_CC :=
HOST_CXX :=

# Libraries are built here.
LIB_DIR := $(proj_dir)/deps
# Library source archives are found here.
LIB_SRC_DIR := $(proj_dir)/deps_src
# Object files are written here.
OBJ_DIR := $(proj_dir)/obj
# Binaries are written here.
BIN_DIR := $(proj_dir)/bin
# Distribution archives are created here.
DIST_DIR := $(proj_dir)
# A temporary directory for distribution archives is created here.
DIST_TMP_DIR := $(proj_dir)/dist

# If true, don't delete build trees when building libraries.
# The build trees occupy extra space.
KEEP_BUILD_TREES := 0

# List flags to filter them out from pkg-config output.
# Same format as `bad_lib_flags` project property: use `%` to match parts of flags;
# use `flag>>>replacement` to replace the flag instead of removing it (can use `%` in replacement too).
# `-mwindows -Wl,-rpath%` are here because SDL2 likes to use them.
BAD_LIB_FLAGS := -mwindows -Wl,-rpath%

# The app name used for packaging. `*` = main project name, `^` = build number.
DIST_NAME = $(APP)_$(TARGET_OS)_$(MODE)_*
# The build number is stored in this file.
DIST_BUILD_NUMBER_FILE := $(proj_dir)/build_number.txt

# The package archive type.
ifeq ($(TARGET_OS),windows)
DIST_ARCHIVE_EXT := .zip
else
DIST_ARCHIVE_EXT := .tar.zst
endif

# Dependency sources are extracted from this archive.
# If this is a filename, it's relative to `$(DIST_DEPS_DIR)`.
# If this is an URL, it's downloaded to `$(DIST_DEPS_DIR)` first.
DIST_DEPS_ARCHIVE := deps.zip
DIST_DEPS_DIR := $(proj_dir)

# Those files and directories are copied next to the executable.
# Since we use `rsync`, a trailing slash has special meaning for directories,
# it means "contents of directory", rather than the directory itself.
ASSETS :=
override default_assets := $(proj_dir)/assets/
ifneq ($(wildcard $(default_assets)),)
ASSETS += $(default_assets)
endif
# Files matching those patterns are ignored when copying assets.
# Note that those patterns are also respected when copying library dependencies, since it's easier to do it this way.
# Note that any matching files that were already copied are not deleted (unlike files not existing in the source), since it's easier to do it this way.
# If you add any new patterns, you need to manually clean copied assets.
ASSETS_IGNORED_PATTERNS := _*

DISABLED_LANGS :=
ifneq ($(TARGET_OS),windows)
DISABLED_LANGS += rc ico
endif

# Compilation commands are written here.
COMMANDS_FILE := $(proj_dir)/compile_commands.json

# System libraries matching those patterns are copied, the rest are ignored.
DIST_COPIED_LIB_PATTERNS := libgcc libstdc++ libc++

# If a system library matches any of `DIST_COPIED_LIB_PATTERNS`, all libraries in the same directory are also considered as matches.
ifeq ($(TARGET_OS),windows)
DIST_ALLOW_ENTIRE_DIR_ON_MATCH := y
else
DIST_ALLOW_ENTIRE_DIR_ON_MATCH :=
endif

# Converts a single absolute path from Linux style to host style.
ifeq ($(HOST_OS),windows)
override abs_path_to_host = $(subst `, ,$(subst $(space),/,$(join $(subst /, ,$(subst $(space),`,$1)),:)))
else
override abs_path_to_host = $1
endif


# Extra files cleaned by the `clean-everything` and `prepare-for-storage` targets. Relative to `$(proj_dir)`.
CLEAN_EXTRA_FILES := .cache


# --- Load config files ---

# For each file in $1, modifies the name to include `.default` before the extension, e.g. `foo.json` -> `foo.default.json`.
override generation_source_for_file = $(foreach x,$1,$(basename $x).default$(suffix $x))

# Local config. Copy it from a default file, if necessary.
LOCAL_CONFIG := $(proj_dir)/local_config.mk
ifneq ($(wildcard $(LOCAL_CONFIG)),)
include $(LOCAL_CONFIG)
else ifneq ($(wildcard $(call generation_source_for_file,$(LOCAL_CONFIG))),)
$(info [Config] Copying `$(call generation_source_for_file,$(LOCAL_CONFIG))` -> `$(LOCAL_CONFIG)`)
$(call safe_shell_exec,cp -f $(call quote,$(call generation_source_for_file,$(LOCAL_CONFIG))) $(call quote,$(LOCAL_CONFIG)))
include $(LOCAL_CONFIG)
endif

# Project config.
P := $(proj_dir)/project.mk
include $P


# --- Fall back to default compiler if not specified ---

# $1 is a clang tool name, e.g. `clang` or `clang++`.
# On success, returns the same tool, possibly suffixed with a version.
# Raises an error on failure.
# Note that we disable the detection when running with `-n`, since the shell function is also disabled in that case.
override find_versioned_tool = $(if $(__cached_tool_$1),,$(call var,__cached_tool_$1 := $(call find_versioned_tool_low,$1)))$(__cached_tool_$1)
override find_versioned_tool_low = $(if $(findstring n,$(single_letter_makeflags)),$1,$(call find_versioned_tool_low2,$1,$(lastword $(sort $(call safe_shell,bash -c 'compgen -c $1' | grep -Po '^$(subst +,\+,$1)(-[0-9]+)?(?=$(HOST_EXT_exe)$$)')))))
override find_versioned_tool_low2 = $(if $2,$2,$(error Can't find $1))

ifeq ($(CC),)
override CC := $(call find_versioned_tool,clang)
endif
ifeq ($(CXX),)
override CXX := $(call find_versioned_tool,clang++)
endif
ifeq ($(LINKER),AUTO)
override LINKER := $(call find_versioned_tool,lld)
endif

# And the host toolchain.
ifeq ($(HOST_CC),)
override HOST_CC := $(call find_versioned_tool,clang)
endif
ifeq ($(HOST_CXX),)
override HOST_CXX := $(call find_versioned_tool,clang++)
endif

# --- Finalize config ---

# Check build mode, update it if necessary.
ifeq ($(mode_list),)
 # No mode list.
 ifeq ($(MODE),)
  # And no mode. Set the default one, since we use this string in file paths.
MODE := generic
 endif
else
 # We have a mode list.
 ifneq ($(running_tab_completion),)
  # If we're running a tab completion and there is no mode, use the default mode, and don't check for errors.
  $(if $(MODE),,$(call var,MODE := generic))
 else
  # Make sure the specified mode is in the list.
  $(if $(filter 0,$(words $(MODE))),$(error Build mode is not set.$(lf)To set the mode once, add `MODE=...` to flags, one of: $(mode_list)$(lf)To set it permanently, use `make remember MODE=...`))
  $(if $(filter-out 1,$(words $(MODE))),$(error MODE must be a single word))
  $(if $(filter $(MODE),generic $(mode_list)),,$(error MODE must `generic` or one of: $(mode_list)))
 endif
endif
# Strip, just in case.
override MODE := $(strip $(MODE))
# Load mode variables.
$(foreach x,$(filter __modevar_$(MODE)_%,$(.VARIABLES)),$(call var,$(patsubst __modevar_$(MODE)_%,%,$x) := $($x)))

# List all executable projects.
override executable_projects := $(foreach x,$(proj_list),$(if $(filter exe,$(__proj_kind_$x)),$x))

# Check app name, update it if necessary.
ifneq ($(running_tab_completion),)
 # If we're running a tab completion and there is no mode, use the default mode, and don't check for errors.
 $(if $(APP),,$(call var,APP := $(firstword $(executable_projects))))
else
 # Make sure the specified app is in the list.
 $(if $(filter 0,$(words $(APP))),$(error Active application is not set.$(lf)To set it once, add `APP=...` to flags, one of: $(executable_projects)$(lf)To set it permanently, use `make remember APP=...`))
 $(if $(filter-out 1,$(words $(APP))),$(error APP must be a single word))
 $(if $(filter $(APP),$(executable_projects)),,$(error APP must be one of: $(executable_projects)))
endif
# Strip, just in case.
override APP := $(strip $(APP))

# Stop ARGS from expanding stuff.
override ARGS := $(value ARGS)


# Note that we can't add the flags to CC, CXX.
# It initially looked like we can, if we then do `-DCMAKE_C_COMPILER=$(subst $(space,;,$(CC))`, but CMake seems to ignore those extra flags. What a shame.
override combined_global_cflags   := $(IMPLICIT_COMMON_FLAGS) $(GLOBAL_COMMON_FLAGS) $(GLOBAL_CFLAGS)
override combined_global_cxxflags := $(IMPLICIT_COMMON_FLAGS) $(GLOBAL_COMMON_FLAGS) $(GLOBAL_CXXFLAGS)
override combined_global_ldflags  := $(IMPLICIT_COMMON_FLAGS) $(if $(LINKER),-fuse-ld=$(LINKER)) $(GLOBAL_LDFLAGS_RPATH) $(GLOBAL_COMMON_FLAGS) $(GLOBAL_LDFLAGS)


# Use this string in paths to mode-specific files.
override os_mode_string := $(TARGET_OS)/$(MODE)

# If `ALLOW_PCH` was 0, make it empty.
override ALLOW_PCH := $(filter-out 0,$(ALLOW_PCH))

# If `KEEP_BUILD_TREES` was 0, make it empty.
override KEEP_BUILD_TREES := $(filter-out 0,$(KEEP_BUILD_TREES))

# Filter out undesired languages.
$(if $(filter-out $(language_list),$(DISABLED_LANGS)),$(error Invalid languages in DISABLED_LANGS: $(filter-out $(language_list),$(DISABLED_LANGS))))
override language_list := $(filter-out $(DISABLED_LANGS),$(language_list))


# --- Print header ---

ifeq ($(TARGET_OS),$(HOST_OS))
$(info [Target] $(APP) ~ $(MODE))
else
$(info [Target] $(HOST_OS)->$(TARGET_OS), $(APP) ~ $(MODE))
endif


# --- Functions to determine library flags ---

# We need this before defining library targets, since those can depend on each other,
# and custom build systems may use those functions determine the flags.

# Expands to `pkg-config` with the proper config variables.
# Not a function.
# See the definion of `PKG_CONFIG_PATH` above for why we set it to a `-` rather than nothing.
override lib_invoke_pkgconfig = PKG_CONFIG_PATH=- PKG_CONFIG_LIBDIR=$(call quote,$(subst $(space),:,$(foreach x,$(all_libs),$(call lib_name_to_base_dir,$x)/$(os_mode_string)/prefix/lib/pkgconfig))) pkg-config --define-prefix

# Given a library name `$1`, returns the pkg-config package names for it.
override lib_find_packages_for = $(if $(__libsetting_only_pkgconfig_files_$(strip $1)),$(__libsetting_only_pkgconfig_files_$(strip $1)),$(basename $(notdir $(wildcard $(call lib_name_to_base_dir,$1)/$(os_mode_string)/prefix/lib/pkgconfig/*.pc))))

# Determine cflags for a list of libraries `$1`.
# We just run pkg-config on all packages of those libraries.
override lib_cflags = $(strip\
	$(if $(filter-out $(all_libs) $(all_lib_stubs),$1),$(error Unknown libraries: $(filter-out $(all_libs) $(all_lib_stubs),$1)))\
	$(call, ### Raw flags will be written here.)\
	$(call var,__raw_flags :=)\
	$(call, ### Pkg-config packages will be written here.)\
	$(call var,__pkgs :=)\
	$(call, ### Run lib_cflags_low for every library.)\
	$(foreach x,$(filter-out $(all_lib_stubs),$1),$(call lib_cflags_low,$(call lib_find_packages_for,$x)))\
	$(call, ### Run pkg-config for libraries that have pkg-config packages.)\
	$(if $(__pkgs),$(call safe_shell,$(lib_invoke_pkgconfig) --cflags $(__pkgs)))\
	$(__raw_flags)\
	$(foreach x,$(filter $(all_lib_stubs),$1),$(__libsetting_stub_cflags_$x))\
	)
override lib_cflags_low = \
	$(if $1,\
		$(call, ### Have pkg-config file, so add this lib to the list of packages.)\
		$(call var,__pkgs += $1)\
	,\
		$(call, ### Use a hardcoded search path.)\
		$(call var,__raw_flags += -I$(call lib_name_to_base_dir,$x)/$(os_mode_string)/prefix/include)\
	)

# Library filename patterns, used by `lib_ldflags` below.
override lib_file_patterns := $(PREFIX_shared)%$(EXT_shared) $(PREFIX_static)%$(EXT_static)

# Determine ldflags for a list of libraries `$1`.
# We try to run pkg-config for each library if available, falling back to manually finding the libraries and linking them.
override lib_ldflags = $(strip\
	$(if $(filter-out $(all_libs) $(all_lib_stubs),$1),$(error Unknown libraries: $(filter-out $(all_libs) $(all_lib_stubs),$1)))\
	$(call, ### Raw flags will be written here.)\
	$(call var,__raw_flags :=)\
	$(call, ### Pkg-config packages will be written here.)\
	$(call var,__pkgs :=)\
	$(call, ### Run lib_ldflags_low for every library.)\
	$(foreach x,$(filter-out $(all_lib_stubs),$1),$(call lib_ldflags_low,$(call lib_find_packages_for,$x)))\
	$(call, ### Run pkg-config for libraries that have pkg-config packages.)\
	$(if $(__pkgs),$(call safe_shell,$(lib_invoke_pkgconfig) --libs $(__pkgs)))\
	$(__raw_flags)\
	$(foreach x,$(filter $(all_lib_stubs),$1),$(__libsetting_stub_ldflags_$x))\
	)
override lib_ldflags_low = \
	$(if $1,\
		$(call, ### Have pkg-config file, so add this lib to the list of packages.)\
		$(call var,__pkgs += $1)\
	,\
		$(call, ### Dir for library search.)\
		$(call var,__dir := $(call lib_name_to_base_dir,$x)/$(os_mode_string)/prefix/lib)\
		$(call, ### Find library filenames.)\
		$(call var,__libs := $(notdir $(wildcard $(subst %,*,$(addprefix $(__dir)/,$(lib_file_patterns))))))\
		$(if $(__libs),\
			$(call, ### Strip prefix and extension.)\
			$(foreach x,$(lib_file_patterns),$(call var,__libs := $(patsubst $x,%,$(__libs))))\
			$(call, ### Convert to flags.)\
			$(call var,__raw_flags += -L$(__dir) $(addprefix -l,$(sort $(__libs))))\
		)\
	)

# $1 is `lib_{c,ld}flags`. $2 is a space-separated list of libraries.
# Calls $1($2) and maintains a global flag cache, to speed up repeated calls.
override lib_cache_flags = $(if $(strip $2),$(call lib_cache_flags_low,$1,$2,__cached_$1_$(subst $(space),@,$(strip $2))))
override lib_cache_flags_low = $(if $($3),,$(call var,$3 := $(call $1,$2)))$($(3))


# --- Generate library targets based on config ---

# $1 is a directory, uses its contents to identify the build system.
# Returns empty string on failure, or one of the names from `buildsystem_detection` variables on success.
override id_build_system = $(word 2,$(subst ->, ,$(firstword $(filter $(patsubst $1/%,%->%,$(wildcard $1/*)),$(buildsystem_detection)))))

# Given library name $1, returns the base directory for storing everything related to it (except for the source archive).
# Can work with lists.
override lib_name_to_base_dir = $(foreach x,$1,$(LIB_DIR)/$(call strip_archive_extension,$(__libsetting_archive_$x)))

# Given library name $1, returns the log path for it. Can work with lists.
override lib_name_to_log_path = $(patsubst %,%/$(os_mode_string)/log.txt,$(call lib_name_to_base_dir,$1))
# Given library name $1, returns the installation prefix for it. Can work with lists.
override lib_name_to_prefix = $(patsubst %,%/$(os_mode_string)/prefix,$(call lib_name_to_base_dir,$1))

# Code for a library target.
# `$(__lib_name)` is the library name.
override define codesnippet_library =
# __lib_name = Library name
$(call var,__ar_name := $(__libsetting_archive_$(__lib_name)))# Library name
$(call var,__ar_path := $(LIB_SRC_DIR)/$(__ar_name))# Archive path
$(call var,__log_path_final := $(call lib_name_to_log_path,$(__lib_name)))# The final log path
$(call var,__log_path := $(__log_path_final).unfinished)# Temporary log path for an unfinished log

# Check that all dependencies are valid.
$(call var,__bad_deps := $(filter-out $(all_libs) $(all_lib_stubs),$(__libsetting_deps_$(__lib_name))))
$(if $(__bad_deps),$(error Unknown dependencies specified for `$(__lib_name)`: $(__bad_deps)))

# Forward the same variables to the target.
$(__log_path_final): override __lib_name := $(__lib_name)
$(__log_path_final): override __ar_name := $(__ar_name)
$(__log_path_final): override __ar_path := $(__ar_path)
$(__log_path_final): override __log_path_final := $(__log_path_final)
$(__log_path_final): override __log_path := $(__log_path)

# NOTE: If adding any useful variable here, document then in `Variables available to build systems` below.

# Builds the library.
.PHONY: lib-$(__lib_name)
lib-$(__lib_name): $(__log_path_final)

# Cleans the library completely.
# `safe_shell_exec` is used here and everywhere to make sure those targets can't run in parallel with building the libraries.
.PHONY: clean-lib-$(__lib_name)
clean-lib-$(__lib_name): override __lib_name := $(__lib_name)
clean-lib-$(__lib_name):
	$(call safe_shell_exec,rm -rf $(call quote,$(call lib_name_to_base_dir,$(__lib_name))))
	@true

# Cleans the library completely for this OS.
.PHONY: clean-lib-$(__lib_name)-this-os
clean-lib-$(__lib_name)-this-os: override __lib_name := $(__lib_name)
clean-lib-$(__lib_name)-this-os:
	$(call safe_shell_exec,rm -rf $(call quote,$(call lib_name_to_base_dir,$(__lib_name))/$(TARGET_OS)))
	@true

# Cleans the library for this OS and mode.
.PHONY: clean-lib-$(__lib_name)-this-os-this-mode
clean-lib-$(__lib_name)-this-os-this-mode: override __lib_name := $(__lib_name)
clean-lib-$(__lib_name)-this-os-this-mode:
	$(call safe_shell_exec,rm -rf $(call quote,$(call lib_name_to_base_dir,$(__lib_name))/$(os_mode_string)))
	@true

# The LIB_SRC_DIR target defined below downloads all dependencies...
$(__ar_path): | $(LIB_SRC_DIR)

# Actually builds the library. Has a pretty alias, defined above.
# One big `strip` around the target makes sure two of them can't run in parallel, I hope.
# Normally that doesn't happen, but once I've seen an error that could only be caused by it.
$(__log_path_final): $(__ar_path) $(call lib_name_to_log_path,$(filter-out $(all_lib_stubs),$(__libsetting_deps_$(__lib_name))))
	$(call, ### Firstly, detect archive type, and stop if unknown, to avoid creating junk.)
	$(call var,__ar_type := $(call archive_classify_filename,$(__ar_name)))
	$(if $(__ar_type),,$(error Don't know this archive extension: `$(__ar_name)`))
	$(call, ### Set variables. Note that the source dir is common for all build modes, to save space.)
	$(call var,__source_dir := $(call lib_name_to_base_dir,$(__lib_name))/source)
	$(call, ### We first extract the archive to this dir, then move the most nested subdir to __source_dir and delete this one.)
	$(call var,__tmp_source_dir := $(call lib_name_to_base_dir,$(__lib_name))/temp_source)
	$(call var,__build_dir := $(call lib_name_to_base_dir,$(__lib_name))/$(os_mode_string)/build)
	$(call var,__install_dir := $(call lib_name_to_prefix,$(__lib_name)))
	$(call log_now,[Library] $(__lib_name))
	$(call, ### Remove old files.)
	$(call safe_shell_exec,rm -rf $(call quote,$(__source_dir)))
	$(call safe_shell_exec,rm -rf $(call quote,$(__tmp_source_dir)))
	$(call safe_shell_exec,rm -rf $(call quote,$(__build_dir)))
	$(call safe_shell_exec,rm -rf $(call quote,$(__install_dir)))
	$(call safe_shell_exec,rm -f $(call quote,$(__log_path_final)))
	$(call safe_shell_exec,rm -f $(call quote,$(__log_path)))
	$(call, ### Make some directories.)
	$(call safe_shell_exec,mkdir -p $(call quote,$(__tmp_source_dir)))
	$(call safe_shell_exec,mkdir -p $(call quote,$(__build_dir)))
	$(call safe_shell_exec,mkdir -p $(call quote,$(__install_dir)))
	$(call safe_shell_exec,mkdir -p $(call quote,$(dir $(__log_path_final))))
	$(call log_now,[Library] >>> Extracting $(__ar_type) archive: $(__ar_name))
	$(call safe_shell_exec,$(call archive_extract-$(__ar_type),$(__ar_path),$(__tmp_source_dir)))
	$(call, ### Move the most-nested source dir to the proper location, then remove the remaining junk.)
	$(call safe_shell_exec,mv $(call quote,$(call most_nested,$(__tmp_source_dir))) $(call quote,$(__source_dir)))
	$(call safe_shell_exec,rm -rf $(call quote,$(__tmp_source_dir)))
	$(call, ### Detect build system.)
	$(call var,__build_sys := $(strip $(if $(__libsetting_build_system_$(__lib_name)),\
		$(__libsetting_build_system_$(__lib_name)),\
		$(call id_build_system,$(__source_dir)))\
	))
	$(if $(filter undefined,$(origin buildsystem-$(__build_sys))),$(error Don't know this build system: `$(__build_sys)`))
	$(call, ### Run the build system.)
	$(buildsystem-$(__build_sys))
	$(call, ### Fix some crap.)
	$(call, ### * Copy pkgconfig files from share/pkgconfig to lib/pkgconfig. Zlib needs this when using CMake, which isn't the only problem with its CMake support.)
	$(call safe_shell_exec,cp -rT $(call quote,$(__install_dir)/share/pkgconfig) $(call quote,$(__install_dir)/lib/pkgconfig) 2>/dev/null || true)
	$(call, ### * Nuke the pkgconfig files, if requested. At least freetype+cmake generates broken files.)
	$(if $(filter-out 0,$(__libsetting_bad_pkgconfig_$(__lib_name))),$(call safe_shell_exec,find -name '*.pc' -delete))
	$(call, ### Delete the build tree, if needed.)
	$(if $(KEEP_BUILD_TREES),,$(call safe_shell_exec,rm -rf $(call quote,$(__build_dir))))
	$(call, ### On success, move the log to the right location.)
	$(call safe_shell_exec,mv $(call quote,$(__log_path)) $(call quote,$(__log_path_final)))
	$(call log_now,[Library] >>> Done)
	@true
endef

# Generate the targets for each library.
$(foreach x,$(all_libs),$(call var,__lib_name := $x)$(eval $(value codesnippet_library)))

.PHONY: libs
libs: $(addprefix lib-,$(all_libs))

# Destroy build/install results for all libraries in the directory, even unknown ones.
.PHONY: clean-libs
clean-libs:
	$(call safe_shell_exec,rm -rf $(call quote,$(LIB_DIR)))
	@true

# Destroy build/install results for all libraries in the directory, for this specific OS and mode.
.PHONY: clean-libs-this-os
clean-libs-this-os:
	$(call safe_shell_exec,rm -rf $(filter $(LIB_DIR)/%,$(wildcard $(LIB_DIR)/*/$(TARGET_OS))))
	@true

# Destroy build/install results for all libraries in the directory, for this specific OS and mode.
.PHONY: clean-libs-this-os-this-mode
clean-libs-this-os-this-mode:
	$(call safe_shell_exec,rm -rf $(filter $(LIB_DIR)/%,$(wildcard $(LIB_DIR)/*/$(os_mode_string))))
	@true


# --- Generate code build targets based on config ---

# Determine language for each project, if not specified.
$(foreach x,$(proj_list),$(if $(__projsetting_lang_$x),,$(call var,__projsetting_lang_$x := cpp)))
# Handle `libs=*`, which means 'all known libraries'.
$(foreach x,$(proj_list),$(if $(findstring $(__projsetting_libs_$x),*),$(call var,__projsetting_libs_$x := $(all_libs) $(all_lib_stubs))))

# Find source files.
override source_file_patterns := $(foreach x,$(language_list),$(language_pattern-$x))
$(foreach x,$(proj_list),$(call var,__proj_allsources_$x := $(sort \
	$(call, ### Individual sources.) \
	$(__projsetting_sources_$x) \
	$(call, ### Source directories.) \
	$(call rwildcard,$(__projsetting_source_dirs_$x),$(source_file_patterns)) \
)))
override all_source_files := $(sort $(foreach x,$(proj_list),$(__proj_allsources_$x)))

# Given filename $1, tries to guess the language. Causes an error on failure.
override guess_lang_from_filename = $(call guess_lang_from_filename_low,$1,$(call guess_lang_from_filename_opt,$1))
override guess_lang_from_filename_low = $(if $2,$2,$(error Unable to guess language from filename: $1))
# Same, but returns an empty string on failure.
override guess_lang_from_filename_opt = $(firstword $(foreach x,$(language_list),$(if $(filter $(subst *,%,$(language_pattern-$x)),$1),$x)))

# A separator for the `bad_lib_flags` project property.
override bad_lib_flags_sep := >>>
# A separator for the `pch` project property.
override pch_rule_sep := ->

# Given project $2 and flag type $1 (cflags or ldflags), returns those flags for all libraries the project depends on.
# The flags are filtered according to the project settings, and also are cached.
override proj_libs_filtered_flags = $(call pairwise_subst,$(bad_lib_flags_sep),$(BAD_LIB_FLAGS) $(__projsetting_bad_lib_flags_$2),$(call lib_cache_flags,lib_$(strip $1),$(__projsetting_libs_$2)))

# Given source filenames $1 and a project $2, returns the resulting dependency output files, if any. Some languages don't generate them.
override source_files_to_dep_outputs = $(strip $(foreach x,$1,$(if $(language_outputs_deps-$(call guess_lang_from_filename,$x)),$(OBJ_DIR)/$(os_mode_string)/$2/$x.d)))

# Given source filenames $1 and a project $2, returns the resulting primary output files. Never returns less elements than in $1.
# and returns only the first output for each file.
override source_files_to_main_outputs = $(patsubst %,$(OBJ_DIR)/$(os_mode_string)/$2/%.o,$1)

# Given source PCH filenames $1 and a project $2, returns the compiled PCH filename.
override pch_files_to_outputs = $(patsubst %,$(OBJ_DIR)/$(os_mode_string)/$2/%.gch,$1)
# Same, but if `ALLOW_PCH` is false, returns the arguments unchanged.
# Note: can't just use `$(if $(ALLOW_PCH),)` here. This kind of check allows us to temporarily disable PCH by wrapping the function in `$(foreach ALLOW_PCH,0,...)`.
override pch_files_to_outputs_or_orig = $(if $(filter-out 0,$(ALLOW_PCH)),$(call pch_files_to_outputs,$1,$2),$1)

# Given a source file $1 and a project $2, returns the PCH header for it, if any.
override pch_header_for_source = $(if $(language_pchflag-$(call guess_lang_from_filename_opt,$1)),$(call find_first_match,$(pch_rule_sep),;,$(subst *,%,$(__projsetting_pch_$2)),$1))
# Given a source file $1 and a project $2, returns the PCH flag for it, if any.
override pch_flag_for_source = $(call pch_flag_for_source_low,$(call pch_files_to_outputs_or_orig,$(call pch_header_for_source,$1,$2),$2))
override pch_flag_for_source_low = $(if $1,-include$(patsubst %.gch,%,$1))

# Given source filenames $1 and a project $2, returns all outputs for them. Might return more elements than in $1, but never less.
# The first resulting element will always be the main output.
override source_files_to_output_list = $(call source_files_to_main_outputs,$1,$2) $(call source_files_to_dep_outputs,$1,$2)

# Given a list of projects $1, returns the link results they produce.
override proj_output_filename = $(foreach x,$1,$(BIN_DIR)/$(os_mode_string)/$(PREFIX_$(__proj_kind_$x))$x$(EXT_$(__proj_kind_$x)))

# Given a list of projects $1, recursively finds all their library dependencies. Recurses both into projects and libraries.
override proj_recursive_lib_deps = $(sort $(call proj_recursive_lib_deps_low_lib,$(sort $(foreach x,$(sort $(call proj_recursive_lib_deps_low_proj,$1)),$(__projsetting_libs_$x)))))
# Helper. For a list of projects, adds all its project dependencies.
override proj_recursive_lib_deps_low_proj = $(if $1,$1 $(call proj_recursive_lib_deps_low_proj,$(foreach x,$1,$(__projsetting_deps_$x))))
# Helper. Recurses into libraries.
override proj_recursive_lib_deps_low_lib = $(if $1,$1 $(call proj_recursive_lib_deps_low_lib,$(foreach x,$1,$(__libsetting_deps_$x))))

# A template for PCH targets.
# Input variables are:
# * __proj - the project name.
# * __src - the header filename.
override define codesnippet_pch =
# Output filename.
override __output := $(call pch_files_to_outputs,$(__src),$(__proj))

$(__output): override __output := $(__output)
$(__output): override __proj := $(__proj)
$(__output): override __lang := $(__projsetting_lang_$(__proj))

$(__output): $(__src) $(call lib_name_to_log_path,$(all_libs))
	$(call log_now,[$(language_name-$(__lang)) PCH] $<)
	@$(call language_command-$(__lang),$<,$@,$(__proj),$(language_pchflag-$(__projsetting_lang_$(__proj))))

-include $(__output:.gch=.d)
endef

# Generate PCH targets.
$(if $(ALLOW_PCH),$(foreach x,$(proj_list),$(call var,__proj := $x)$(foreach y,$(__projsetting_pch_$x),$(call var,__src := $(lastword $(subst $(pch_rule_sep), ,$y)))$(eval $(value codesnippet_pch)))))

# A template for object file targets.
# Input variables:
# `__src` - the source file.
# `__proj` - the project name.
override define codesnippet_object =
# Output filenames.
override __outputs := $(call source_files_to_main_outputs,$(__src),$(__proj))
# I wanted to include the dep file in the target, but it causes the makefile itself to be rebuilt. :(
# override __outputs := $(call source_files_to_output_list,$(__src),$(__proj))
# The compiled PCH, if any.
override __pch := $(call pch_files_to_outputs_or_orig,$(call pch_header_for_source,$(__src),$(__proj)),$(__proj))

$(__outputs) &: override __outputs := $(__outputs)
$(__outputs) &: override __proj := $(__proj)
$(__outputs) &: override __lang := $(call guess_lang_from_filename,$(__src))
$(__outputs) &: override __pch := $(__pch)

$(__outputs) &: $(__src) $(__pch) $(call lib_name_to_log_path,$(all_libs))
	$(call log_now,[$(language_name-$(__lang))] $<)
	@$(call language_command-$(__lang),$<,$(firstword $(__outputs)),$(__proj),,output_deps)

-include $(call source_files_to_dep_outputs,$(__src),$(__proj))
endef

# Generate object file targets.
$(foreach x,$(proj_list),$(call var,__proj := $x)$(foreach y,$(__proj_allsources_$x),$(call var,__src := $y)$(eval $(value codesnippet_object))))

# A template for link targets.
# The only input variable is `__proj`, the project name.
override define codesnippet_link =
# Link result.
override __filename := $(call proj_output_filename,$(__proj))

# Check that we only depend on shared library projects.
$(foreach x,$(__projsetting_deps_$(__proj)),$(if $(filter shared,$(__proj_kind_$x)),,$(error Can't depend on `$x`, it's not a library project)))

# A user-friendly link target. It also updates assets.
.PHONY: build-$(__proj)
build-$(__proj): $(__filename) sync-libs-and-assets

# The actual link target.
$(__filename): override __proj := $(__proj)
$(__filename): $(call source_files_to_main_outputs,$(__proj_allsources_$(__proj)),$(__proj)) $(call proj_output_filename,$(__projsetting_deps_$(__proj)))
	$(call log_now,[$(proj_kind_name-$(__proj_kind_$(__proj)))] $@)
	@$(language_link-$(__projsetting_lang_$(__proj))) $(if $(filter shared,$(__proj_kind_$(__proj))),-shared) -o $@ $(filter %.o,$^) \
		$(call proj_libs_filtered_flags,ldflags,$(__proj)) \
		$(combined_global_ldflags) $(PROJ_COMMON_FLAGS) $(PROJ_LDFLAGS) $(__projsetting_common_flags_$(__proj)) $(__projsetting_ldflags_$(__proj)) \
		-L$(call quote,$(BIN_DIR)/$(os_mode_string)) $(patsubst $(PREFIX_shared)%$(EXT_shared),-l%,$(notdir $(call proj_output_filename,$(__projsetting_deps_$(__proj)))))

ifeq ($(__proj_kind_$(__proj)),exe)
# A target to run the project.
.PHONY: run-$(__proj)
run-$(__proj): override __proj := $(__proj)
run-$(__proj): override __filename := $(__filename)
run-$(__proj): build-$(__proj)
	$(call log_now,[Running] $(__proj))
	@$(run_without_buffering)$(PROJ_RUNTIME_ENV) $(__projsetting_runtime_env_$(__proj)) $(__filename) $(ARGS)

# A target to run the project without compiling it.
.PHONY: run-old-$(__proj)
run-old-$(__proj): override __proj := $(__proj)
run-old-$(__proj): override __filename := $(__filename)
run-old-$(__proj):
	$(call log_now,[Running existing build] $(__proj))
	@$(run_without_buffering)$(PROJ_RUNTIME_ENV) $(__projsetting_runtime_env_$(__proj)) $(__filename) $(ARGS)
endif

# Target to clean the project.
clean-this-os-this-mode-$(__proj): override __proj := $(__proj)
clean-this-os-this-mode-$(__proj): override __filename := $(__filename)
clean-this-os-this-mode-$(__proj):
	$(call safe_shell_exec,rm -rf $(call quote,$(__filename)))
	$(call safe_shell_exec,rm -rf $(call quote,$(OBJ_DIR)/$(os_mode_string)/$(__proj)))
	@true
endef

# Generate link targets.
$(foreach x,$(proj_list),$(call var,__proj := $x)$(eval $(value codesnippet_link)))

# A list of targets that need directories to be created for them.
override targets_needing_dirs :=
# * The object files:
override targets_needing_dirs += $(foreach x,$(proj_list),$(call source_files_to_output_list,$(__proj_allsources_$x),$x))
# * The link results:
override targets_needing_dirs += $(foreach x,$(proj_list),$(call proj_output_filename,$x))
# * Compiled PCHs:
override targets_needing_dirs += $(if $(ALLOW_PCH),$(foreach x,$(proj_list),$(call pch_files_to_outputs,$(foreach y,$(__projsetting_pch_$x),$(lastword $(subst $(pch_rule_sep), ,$y))),$x)))
# Generate the directory targets.
$(foreach x,$(targets_needing_dirs),$(eval $x: | $(dir $x)))
$(foreach x,$(sort $(dir $(targets_needing_dirs))),$(eval $x: ; @mkdir -p $(call quote,$x)))

# Add targets for the current app.
.PHONY: build-current
build-current: build-$(APP)
.PHONY: run-current
run-current: run-$(APP)
.PHONY: run-old-current
run-old-current: run-old-$(APP)

# "Build all" target.

.PHONY: build-all
build-all: $(foreach x,$(proj_list),build-$x)

# Cleaning targets. Those ignore libraries, unless specified otherwise.

.PHONY: clean
clean:
	$(call safe_shell_exec,rm -rf $(call quote,$(BIN_DIR)) $(call quote,$(OBJ_DIR)))
	@true

.PHONY: clean-this-os
clean-this-os:
	$(call safe_shell_exec,rm -rf $(call quote,$(BIN_DIR)/$(TARGET_OS)) $(call quote,$(OBJ_DIR)/$(TARGET_OS)))
	@true

.PHONY: clean-this-os-this-mode
clean-this-os-this-mode:
	$(call safe_shell_exec,rm -rf $(call quote,$(BIN_DIR)/$(os_mode_string)) $(call quote,$(OBJ_DIR)/$(os_mode_string)))
	@true

.PHONY: clean-including-libs
clean-including-libs: clean clean-libs

.PHONY: clean-this-os-including-libs
clean-this-os-including-libs: clean-this-os clean-libs-this-os

.PHONY: clean-this-os-this-mode-including-libs
clean-this-os-this-mode-including-libs: clean-this-os-this-mode clean-libs-this-os-this-mode

# Destroy all compiled PCHs.
.PHONY: clean-pch
clean-pch:
	$(call safe_shell_exec,find $(call quote,$(OBJ_DIR)) -name '*.gch' -type f -delete)
	@true

# `compile_commands.json` target.

# Double-quotes a string.
override doublequote = "$(subst `,\\,$(subst ",\",$(subst \,`,$1)))"

# Writes compile commands for project $1 to the COMMANDS_FILE.
# If `__commands_first` is non-empty, doesn't write the comma (intended for the first element).
# Consults `__commands_files` for the files that have been already generated. Ignores repeated files.
override write_commands_for_project = \
	$(foreach x,$(__proj_allsources_$1),\
		$(if $(filter $x,$(__commands_files)),,\
			$(call var,__commands_files += $x)\
			$(call, ### Note the trick to set ALLOW_PCH to 0 just for this line.)\
			$(file >>$(COMMANDS_FILE),   $(if $(__commands_first),$(call var,__commands_first :=) ,$(comma)){"directory": $(__curdir), "file": $(call doublequote,$(call abs_path_to_host,$(abspath $x))), "command": $(call doublequote,$(foreach ALLOW_PCH,0,$(call language_command-$(call guess_lang_from_filename,$x),$x,,$1)))})\
		)\
	)

.PHONY: commands
commands:
	$(call var,__curdir := $(call doublequote,$(call abs_path_to_host,$(abspath $(proj_dir)))))
	$(call var,__commands_first := 1)
	$(call var,__commands_files := 1)
	$(file >$(COMMANDS_FILE),[)
	$(call, ### Note that we generate commands for APP first, to give it priority. We deduplicate the commands ourselves.)
	$(call write_commands_for_project,$(APP))
	$(foreach x,$(filter-out $(APP),$(proj_list)),$(call write_commands_for_project,$x))
	$(file >>$(COMMANDS_FILE),])
	@true

# --- Mode-switching target ---

# Added before the current mode when saving it to the config.
override config_prefix_mode := MODE :=
# Added before the current app when saving it to the config.
override config_prefix_app := APP :=

# Those files are regenerated on mode and/or target change. E.g. `foo.json` is generated from `foo.default.json`.
# In them, following replacements are made:
# * `<EXECUTABLE>` -> the output executable of the current project
# * `<ARGS>` -> the executable flags chosen by the user
# * `/*<ENV_JSON>*/` -> the list of environment variables as a JSON, e.g. `"a":"b", "c":"d",`.
GENERATE_ON_TARGET_CHANGE := $(proj_dir)/.vscode/launch.json
# Remove files with missing original files.
override GENERATE_ON_TARGET_CHANGE := $(foreach x,$(GENERATE_ON_TARGET_CHANGE),$(if $(wildcard $(call generation_source_for_file,$x)),$x))

# Writes the current MODE and APP to the local config.
.PHONY: remember
remember:
	$(call, ### Filter out existing stuff from the local config.)
	$(call safe_shell_exec,awk -i inplace '/MODE :=/ {next} /APP :=/ {next} /ARGS :=/ {next} {print}' $(call quote,$(LOCAL_CONFIG)))
	$(call, ### Write new values.)
	$(call, ### We used to have some clever code that used `sed` to replace the existing values, if any, but that made it harder to handle wonky flags.)
	$(file >>$(LOCAL_CONFIG),MODE := $(MODE))\
	$(file >>$(LOCAL_CONFIG),APP := $(APP))\
	$(file >>$(LOCAL_CONFIG),ARGS := $(subst $,$$$$,$(ARGS)))\
	$(call, ### Convert the list of runtime env variables to JSON, to bake into the generated files.)\
	$(call var,__env_as_json := $(foreach x,$(PROJ_RUNTIME_ENV) $(__projsetting_runtime_env_$(APP)),$(call var,__name := $(firstword $(subst =, ,$x)))"$(__name)":"$(patsubst $(__name)=%,%,$x)"$(comma)))
	$(call, ### Regenerate some files.)\
	$(foreach x,$(GENERATE_ON_TARGET_CHANGE),\
		$(call safe_shell_exec,cp $(call quote,$(call generation_source_for_file,$x)) $(call quote,$x))\
		$(call safe_shell_exec,sed -i 's|<EXECUTABLE>|$(call proj_output_filename,$(APP))|' $(call quote,$x))\
		$(call, ### Note that we replace \->\\ twice. The first replacement is for JSON, and the second is for Sed.)\
		$(call safe_shell_exec,sed -i 's|<ARGS>|'$(call quote,$(subst \,\\,$(subst ",\",$(subst \,\\,$(ARGS)))))'|' $(call quote,$x))\
		$(call safe_shell_exec,sed -i 's|/\*<ENV_JSON>\*/|'$(call quote,$(subst \,\\,$(__env_as_json)))'|' $(call quote,$x))\
	)
	@true

# --- Copy assets target ---

# Uses `rsync` to copy assets and library dependencies to the directory $1. If $2 is true, also copy our library dependencies.
# We don't run LDD, and instead copy all requested dependencies. We also patchelf them, if needed.
# We delete all mismatching files in the target directory, except for the project outputs, if any.
# Note that we prefix project outputs with `/`, to indicate that rsync shouldn't match those filenames in subdirectories.
# Note: if there's not a single input directory suffixed with `/`, `rsync` refuses to delete extra files in the target directory.
#   This shouldn't affect us, as we usually only copy directories.
override copy_assets_and_libs_to = \
	$(call var,__lib_deps := $(strip $(foreach x,$(foreach x,$(call proj_recursive_lib_deps,$(proj_list)),$(wildcard $(call lib_name_to_base_dir,$x)/$(os_mode_string)/prefix/$(SHARED_LIB_DIR_IN_PREFIX)/*)),$(if $(call IS_SHARED_LIB_FILENAME,$x),$x))))\
	$(call safe_shell_exec,rsync -Lrt --delete $(foreach x,$(ASSETS_IGNORED_PATTERNS),--exclude $x) $(foreach x,$(proj_list),--exclude $(call quote,/$(notdir $(call proj_output_filename,$x)))) $(foreach x,$(__lib_deps),--exclude $(call quote,/$(notdir $x))) $(ASSETS) $(call quote,$1))\
	$(if $2,$(foreach x,$(__lib_deps),$(if $(strip $(call safe_shell,cp -duv $(call quote,$x) $(call quote,$1))),$(info [Copy library] $(notdir $x))$(if $(PATCHELF),$(if $(filter 0,$(call shell_status,test ! -L $(call quote,$1/$(notdir $x)))),$(call safe_shell_exec,$(PATCHELF) $(call quote,$1/$(notdir $x))))))))

# Copies libraries and `ASSETS` to the current bin directory, ignoring any files matching `ASSETS_IGNORED_PATTERNS`.
.PHONY: sync-libs-and-assets
sync-libs-and-assets: $(call lib_name_to_log_path,$(all_libs))
	$(call copy_assets_and_libs_to,$(BIN_DIR)/$(os_mode_string),1)
	@true


# --- Packaging target ---

# Commands to produce the package archive. $1 is the source directory, $2 is a list of files in that directory, $3 is the resulting archive.

# Here we use shell redirection to force-overwrite the file instead of updating it. AND it makes it easier to determine the target path.
override dist_command-.zip = (cd $(call quote,$1) && zip -qr9 - $2) >$(call quote,$3)
override dist_command-.tar.zst = ZSTD_CLEVEL=13 tar --zstd -C $(call quote,$1) -cf $(call quote,$3) $2

# Decrement the build number, don't do anything else.
# Do this between consecutive builds that must have the same build number.
.PHONY: repeat-build-number
repeat-build-number:
	$(call var,__buildnumber := $(file <$(DIST_BUILD_NUMBER_FILE)))
	$(if $(__buildnumber),,$(call var,__buildnumber := 0))
	$(call var,__buildnumber := $(call safe_shell,echo $(call quote,$(__buildnumber)-1) | bc))
	$(file >$(DIST_BUILD_NUMBER_FILE),$(__buildnumber))
	$(info Will repeat build number: $(__buildnumber))
	@true

# Archives library sources into a single archive. The filename without path is taken from `DIST_DEPS_ARCHIVE`. The directory is `DIST_DEPS_DIR`.
.PHONY: dist-deps
dist-deps:
	$(call var,__ar_ext := $(suffix $(notdir $(DIST_DEPS_ARCHIVE))))
	$(if $(dist_command-$(__ar_ext)),,$(error Don't know how to create a `$(__ar_ext)` archive))
	$(call safe_shell_exec,$(call dist_command-$(__ar_ext),$(LIB_SRC_DIR),$(patsubst $(LIB_SRC_DIR)/%,%,$(wildcard $(LIB_SRC_DIR)/*)),$(DIST_DEPS_DIR)/$(notdir $(DIST_DEPS_ARCHIVE))))
	@true

# Build and package the app, using the current mode. Then increment the build number.
.PHONY: dist
dist: build-current
	$(call var,__main_exe := $(call proj_output_filename,$(APP)))
	$(call var,__libs_copied :=)
	$(call, ### Run LDD.)
	$(call, ### Note abspath on the executable. Without it, the returned paths can contain `.`, which messes with our library classification.)
	$(call var,__libs := $(call safe_shell,$(call proj_library_path_prefix,$(APP)) $(LDD) $(call quote,$(abspath $(__main_exe))) | sed -E 's/^\s*([^ =]*)( => ([^ ]*))? .0x.*$$/###\1=>\3/g' | grep -Po '(?<=^###).*$$'))
	$(info [Dist] Parsed LDD output: $(__libs))
	$(call, ### Remove library names from output, leave only paths.)
	$(call var,__libs := $(foreach x,$(__libs),$(word 2,$(subst =>, ,$x))))
	$(call, ### Preprocess paths. On native Windows this runs cygpath to convert them to unix-style. On Wine this runs Winepath.)
	$(if $(value LDD_PREPROCESS_PATHS),$(call var,__libs := $(call safe_shell,$(call LDD_PREPROCESS_PATHS,$(foreach x,$(__libs),$(call quote,$x))))))
	$(call, ### Make paths relative. At least Quasi-MSYS2 win-ldd needs this.)
	$(call var,__libs := $(subst $(abspath $(proj_dir)),$(proj_dir),$(__libs)))
	$(info [Dist] Preprocessed LDD output: $(__libs))
	$(info [Dist] --- Following libraries will be copied:)
	$(call, ### Filter our own library projects and library dependencies.)
	$(call var,__libs_proj := $(strip $(foreach x,$(__libs),$(if $(filter $(BIN_DIR)/$(os_mode_string)/%,$x),$x))))
	$(call var,__libs := $(filter-out $(__libs_proj),$(__libs)))
	$(if $(__libs_proj),$(info [Dist] Our libraries: $(notdir $(__libs_proj))))
	$(call var,__libs_copied += $(__libs_proj))
	$(call, ### Filter good system libraries.)
	$(call var,__libs_sys := $(strip $(foreach x,$(__libs),$(if $(call filter_substr,$(DIST_COPIED_LIB_PATTERNS),$x),$x))))
	$(call var,__libs := $(filter-out $(__libs_sys),$(__libs)))
	$(if $(__libs_sys),$(info [Dist] Allowed system libraries: $(notdir $(__libs_sys))))
	$(call var,__libs_copied += $(__libs_sys))
	$(call, ### If necessary, add libraries from system directories.)
	$(call var,__libs_extra_sys := )
	$(if $(DIST_ALLOW_ENTIRE_DIR_ON_MATCH),\
		$(call var,__lib_sys_dirs := $(sort $(foreach x,$(__libs_sys),$(dir $x))))\
		$(info [Dist] Extra allowed system directories: $(__lib_sys_dirs))\
		$(call var,__libs_extra_sys := $(strip $(foreach x,$(__libs),$(if $(filter $(addsuffix %,$(__lib_sys_dirs)),$x),$x))))\
		$(call var,__libs := $(filter-out $(__libs_extra_sys),$(__libs)))\
		$(if $(__libs_extra_sys),$(info [Dist] Extra system libraries: $(notdir $(__libs_extra_sys))))\
		$(call var,__libs_copied += $(__libs_extra_sys))\
	)
	$(call, ### Print rejected libs.)
	$(info [Dist] --- Following libraries will be ignored:)
	$(info [Dist] $(notdir $(__libs)))
	$(call, ### Read build number.)
	$(call var,__buildnumber := $(file <$(DIST_BUILD_NUMBER_FILE)))
	$(if $(__buildnumber),,$(call var,__buildnumber := 0))
	$(call, ### Clean target dir.)
	$(call safe_shell_exec,rm -rf $(call quote,$(DIST_TMP_DIR)))
	$(call var,__target_name := $(subst *,$(__buildnumber),$(DIST_NAME)))
	$(call var,__target_dir := $(DIST_TMP_DIR)/$(__target_name))
	$(call safe_shell_exec,mkdir -p $(call quote,$(__target_dir)))
	$(call, ### Copy assets. This must be first, because the command will erase some other files from target directory.)
	$(call copy_assets_and_libs_to,$(__target_dir))
	$(call, ### Copy the executable.)
	$(call safe_shell_exec,cp $(call quote,$(__main_exe)) $(call quote,$(__target_dir)))\
	$(if $(PATCHELF),$(call safe_shell_exec,$(PATCHELF) $(call quote,$(__target_dir)/$(notdir $(__main_exe)))))\
	$(call, ### Copy libraries.)
	$(foreach x,$(__libs_copied),\
		$(call safe_shell_exec,cp $(call quote,$x) $(call quote,$(__target_dir)))\
		$(if $(PATCHELF),$(call safe_shell_exec,$(PATCHELF) $(call quote,$(__target_dir)/$(notdir $x))))\
	)
	$(call, ### Make an archive.)
	$(if $(dist_command-$(DIST_ARCHIVE_EXT)),,$(error Unknown archive type: $(DIST_ARCHIVE_EXT)))
	$(call safe_shell_exec,$(call dist_command-$(DIST_ARCHIVE_EXT),$(DIST_TMP_DIR),$(call quote,$(__target_name)),$(DIST_DIR)/$(__target_name)$(DIST_ARCHIVE_EXT)))
	$(info $(lf)[Dist] Produced:  $(__target_name)$(lf))
	$(call, ### Increment and write build number. Makes more sense to do it now, in case something reads it during build.)
	$(call var,__buildnumber := $(call safe_shell,echo $(call quote,$(__buildnumber)+1) | bc))
	$(file >$(DIST_BUILD_NUMBER_FILE),$(__buildnumber))
	@true


# --- Full clean targets ---

# Cleans everything.
.PHONY: clean-everything
clean-everything:
	$(call safe_shell_exec,rm -rf $(call quote,$(BIN_DIR)))
	$(call safe_shell_exec,rm -rf $(call quote,$(OBJ_DIR)))
	$(call safe_shell_exec,rm -rf $(call quote,$(LIB_DIR)))
	$(call safe_shell_exec,rm -rf $(call quote,$(DIST_TMP_DIR)))
	$(call safe_shell_exec,rm -rf $(call quote,$(COMMANDS_FILE)))
	$(foreach x,$(CLEAN_EXTRA_FILES),$(call safe_shell_exec,rm -rf $(call quote,$(proj_dir)/$x)))
	@true

# Cleans everything, and additionally archives the library sources into a single file, and deletes the separate archives.
.PHONY: prepare-for-storage
ifneq ($(wildcard $(LIB_SRC_DIR)),)
prepare-for-storage: dist-deps
endif
prepare-for-storage: clean-everything
	$(call safe_shell_exec,rm -rf $(call quote,$(LIB_SRC_DIR)))
	@true


# --- Auto-download dependency sources ---

$(LIB_SRC_DIR):
	$(call var,__target_file := $(DIST_DEPS_DIR)/$(notdir $(DIST_DEPS_ARCHIVE)))
	$(call var,__download :=)
	$(info [Deps] Populating `$@` with dependency sources.)
	$(if $(findstring ://,$(DIST_DEPS_ARCHIVE)),\
		$(info [Deps] Using archive at: $(DIST_DEPS_ARCHIVE))\
		$(if $(wildcard $(__target_file)),\
			$(info [Deps] The file already exists at `$(__target_file)`, will use it.)\
		,\
			$(call var,__download := yes)\
		)\
	,\
		$(info [Deps] Using archive at: $(__target_file))\
	)
	$(if $(__download),@$(run_without_buffering)wget -q --show-progress $(call quote,$(DIST_DEPS_ARCHIVE)) -O $(call quote,$(__target_file)))
	$(call var,__deps_ar_type := $(call archive_classify_filename,$(__target_file)))
	$(if $(__deps_ar_type),,$(error Don't know this archive extension))
	@$(call archive_extract-$(__deps_ar_type),$(__target_file),$(LIB_SRC_DIR))


# --- Build system definitions ---

# Variables available to build systems:
# __lib_name - library name.
# __log_path - the log file you should append to.
# __build_dir - the directory you should build in.
# __install_dir - the prefix you should install to.
# __source_dir - the source location. We automatically descend into subdirectories, if there is nothing else next to them.

# How to define a build system:
# * Create a variable named `buildsystem-<name>`, with a sequence of `$(call safe_shell_exec,)` commands, using the variables listed above.
# * If you want to, modify `buildsystem_detection` above to auto-detect your build system.

# Copies the files you tell it to copy.
# Must specify the `copy_files` setting, which is a space-separated list of `pattern->dir`,
# where `pattern` is relative to the source directory and can contain `*`, and `dir` is the target directory relative to the installation prefix.
override buildsystem-copy_files = \
	$(call log_now,[Library] >>> Copying files...)\
	$(call, ### Make sure we know what files to copy.)\
	$(if $(__libsetting_copy_files_$(__lib_name)),,$(error Must specify the `copy_files` setting for the `copy_files` build system))\
	$(call, ### Actually copy the files.)\
	$(foreach x,$(__libsetting_copy_files_$(__lib_name)),\
		$(call var,__bs_target := $(call quote,$(__install_dir)/$(word 2,$(subst ->, ,$x))))\
		$(call safe_shell_exec,mkdir -p $(__bs_target))\
		$(call safe_shell_exec,cp -r $(wildcard $(__source_dir)/$(word 1,$(subst ->, ,$x))) $(__bs_target))\
	)\
	$(call, ### Destroy the original extracted directory to save space.)\
	$(call safe_shell_exec,rm -rf $(call quote,$(__source_dir)))\
	$(file >$(__log_path),)

# List separator for CMake. This does indeed look backwards.
ifeq ($(HOST_OS),windows)
cmake_host_sep := :
else
cmake_host_sep := ;
endif

override buildsystem-cmake = \
	$(call log_now,[Library] >>> Configuring CMake...)\
	$(call, ### Add dependency include directories to compiler flags. Otherwise OpenAL can't find SDL2.)\
	$(call, ### Note that we're not single-quoting paths here, as it doesn't work in native Windows builds. Should probably use double-quotes, but junk in paths doesn't work anyway because of Zlib and such.)\
	$(call var,__bs_include_paths := $(foreach x,$(call lib_name_to_prefix,$(__libsetting_deps_$(__lib_name))),-I$(call abs_path_to_host,$(abspath $x)/include)))\
	$(call var,__bs_cflags := $(combined_global_cflags) $(__libsetting_common_flags_$(__lib_name)) $(__libsetting_cflags_$(__lib_name)) $(__bs_include_paths))\
	$(call var,__bs_cxxflags := $(combined_global_cxxflags) $(__libsetting_common_flags_$(__lib_name)) $(__libsetting_cxxflags_$(__lib_name)) $(__bs_include_paths))\
	$(call var,__bs_ldflags := $(combined_global_ldflags) $(__libsetting_common_flags_$(__lib_name)) $(__libsetting_ldflags_$(__lib_name)))\
	$(call safe_shell_exec,\
		$(call, ### Using the env variables instead of the CMake variables to silence the unused variable warnings. Also this is less verbose.)\
		CC=$(call quote,$(CC)) CXX=$(call quote,$(CXX))\
		CFLAGS=$(call quote,$(__bs_cflags)) CXXFLAGS=$(call quote,$(__bs_cxxflags)) LDFLAGS=$(call quote,$(__bs_ldflags))\
		$(call, ### Resetting the pkg-config variables here prevents freetype from finding the system harfbuzz, and possibly more.)\
		$(call, ### See their definitions above in this makefile for why we set them to `-` rather than empty strings.)\
		PKG_CONFIG_PATH=- PKG_CONFIG_LIBDIR=- \
		cmake\
		-S $(call quote,$(__source_dir))\
		-B $(call quote,$(__build_dir))\
		-Wno-dev\
		$(call, ### Weird semi-documented flags. Helps at least for freetype, ogg, vorbis.)\
		-DBUILD_SHARED_LIBS=ON\
		$(call, ### Specifying an invalid build type disables built-in flags.)\
		-DCMAKE_BUILD_TYPE=Custom\
		-DCMAKE_INSTALL_PREFIX=$(call quote,$(__install_dir))\
		$(call, ### Fedora installs to `lib64` by default, which breaks our stuff. Apparently everyone except Debian does it, maybe we should too? Hmm.)\
		$(call, ### Some libraries that don't install anything warn about this flag being unused.)\
		$(call, ### I don't see any workaround though, short of switching to `lib64` ourselves, since setting the `LIBDIR` environment variable has no effect.)\
		-DCMAKE_INSTALL_LIBDIR=lib\
		$(call, ### I'm not sure why abspath is needed here, but stuff doesn't work otherwise. Tested on libvorbis depending on libogg.)\
		$(call, ### Note the fancy logic that attempts to support spaces in paths.)\
		-DCMAKE_PREFIX_PATH=$(call quote,$(abspath $(__install_dir))$(subst $(space)$(cmake_host_sep),$(cmake_host_sep),$(foreach x,$(call lib_name_to_prefix,$(__libsetting_deps_$(__lib_name))),$(cmake_host_sep)$(abspath $x))))\
		$(call, ### Prevent CMake from finding system packages. Tested on freetype2, which finds system zlib otherwise.)\
		$(call, ### Note: this disables various ..._SYSTEM_..., variables, so we can't use those, even though they would be more appropriate otherwise.)\
		$(call, ### We can't not disable those, enabling this seems to add parents of PATH directories to the prefixes, and so on.)\
		-DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF\
		$(call, ### This is only useful when cross-compiling, to undo the effects of CMAKE_FIND_ROOT_PATH in a toolchain file, which otherwise restricts library search to that path.)\
		$(call, ### We set the value to /, except on Windows hosts, where it's set to the drive letter, since the plain slash doesn't work.)\
		$(call, ### This also resets the install path, so we need to specify it again with installing.)\
		-DCMAKE_STAGING_PREFIX=$(if $(filter windows,$(HOST_OS)),$(firstword $(subst \, ,$(call safe_shell,cygpath -w $(call quote,$(CURDIR))))),/)\
		$(call, ### On Windows hosts, ignore PATH completely. Windows hosts are unusual in that they find not only executables,)\
		$(call, ### but also libraries in the prefixes that have their `bin` directories added to PATH. This is issue https://gitlab.kitware.com/cmake/cmake/-/issues/24036)\
		$(call, ### Disabling this also stops CMake from finding any programs in PATH. We compensate by setting `CMAKE_PROGRAM_PATH`,)\
		$(call, ### but it's ignored for Binutils search, and possibly others. But for those, CMake magically searches the `bin` directory of the specified compiler.)\
		$(call, ### On Quasi-MSYS2 it causes CMake to be unable to find AR and others, since the compiler is stored separately, but on Windows it's not a problem.)\
		$(if $(filter windows,$(HOST_OS)),-DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF -DCMAKE_PROGRAM_PATH=$(call quote,$(subst :,$(cmake_host_sep),$(PATH))))\
		$(call, ### This is needed because of `-DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF`.)\
		$(if $(CMAKE_BUILD_TOOL),-DCMAKE_MAKE_PROGRAM=$(call quote,$(CMAKE_BUILD_TOOL)))\
		$(if $(CMAKE_GENERATOR),$(call quote,-G$(CMAKE_GENERATOR)))\
		$(__libsetting_cmake_flags_$(__lib_name))\
		>>$(call quote,$(__log_path))\
	)\
	$(call log_now,[Library] >>> Building...)\
	$(call safe_shell_exec,cmake --build $(call quote,$(__build_dir)) 2>&1 >>$(call quote,$(__log_path)) -j$(JOBS))\
	$(call log_now,[Library] >>> Installing...)\
	$(call, ### Note that we must specify the install path again, see the use of CMAKE_STAGING_PREFIX above.)\
	$(call safe_shell_exec,cmake --install $(call quote,$(__build_dir)) --prefix $(call quote,$(__install_dir)) >>$(call quote,$(__log_path)))\

override buildsystem-configure_make = \
	$(call, ### A list of env variables we use. Note explicit pkg-config stuff. At least freetype needs it, and fails to find pkgconfig files in the prefix otherwise.)\
	$(call, ### Note that we only need to set prefix for this single library, since we copy all dependencies here anyway.)\
	$(call var,__bs_shell_vars := $(foreach f,CC CXX CPP LD CPPFLAGS,$f=$(call quote,$($f))) \
		CFLAGS=$(call quote,$(combined_global_cflags) $(__libsetting_common_flags_$(__lib_name)) $(__libsetting_cflags_$(__lib_name))) \
		CXXFLAGS=$(call quote,$(combined_global_cxxflags) $(__libsetting_common_flags_$(__lib_name)) $(__libsetting_cxxflags_$(__lib_name))) \
		LDFLAGS=$(call quote,$(combined_global_ldflags) $(__libsetting_common_flags_$(__lib_name)) $(__libsetting_ldflags_$(__lib_name))) \
		$(call, ### See the definion of `PKG_CONFIG_PATH` above for why we set it to a space rather than nothing.)\
		PKG_CONFIG_PATH=' ' PKG_CONFIG_LIBDIR=$(call quote,$(abspath $(__install_dir)/lib/pkgconfig)) $(__libsetting_configure_vars_$(__lib_name)) \
	)\
	$(call, ### Since we can't configure multiple search prefixes, like we do with CMAKE_SYSTEM_PREFIX_PATH,)\
	$(call, ### we copy the prefixes of our dependencies to our own prefix.)\
	$(foreach x,$(call lib_name_to_prefix,$(__libsetting_deps_$(__lib_name))),$(call safe_shell_exec,cp -rT $(call quote,$x) $(call quote,$(__install_dir))))\
	$(call log_now,[Library] >>> Running `./configure`...)\
	$(call, ### Note abspath on the prefix, I got an error explicitly requesting an absolute path. Tested on libvorbis.)\
	$(call, ### Note the jank `cd`. It seems to allow out-of-tree builds.)\
	$(call safe_shell_exec,(cd $(call quote,$(__build_dir)) && $(__bs_shell_vars) $(call quote,$(abspath $(__source_dir)/configure)) --prefix=$(call quote,$(abspath $(__install_dir))) $(__libsetting_configure_flags_$(__lib_name))) >>$(call quote,$(__log_path)))\
	$(call log_now,[Library] >>> Building...)\
	$(call safe_shell_exec,$(__bs_shell_vars) make -C $(call quote,$(__build_dir)) -j$(JOBS) -Otarget 2>&1 >>$(call quote,$(__log_path)))\
	$(call log_now,[Library] >>> Installing...)\
	$(call, ### Note DESTDIR. We don't want to install to prefix yet, since we've copied our dependencies there.)\
	$(call, ### Note abspath for DESTDIR. You get an error otherwise, explicitly asking for an absolute one. Tested on libvorbis.)\
	$(call, ### Note redirecting stderr. Libtool warns when DESTDIR is non-empty, which is useless: "remember to run libtool --finish")\
	$(call safe_shell_exec,$(__bs_shell_vars) DESTDIR=$(call quote,$(abspath $(__source_dir)/__tmp_prefix)) make -C $(call quote,$(__build_dir)) install 2>&1 >>$(call quote,$(__log_path)))\
	$(call, ### Now we can clean the prefix. Can't do it before installing, because erasing headers from there would trigger a rebuild.)\
	$(call safe_shell_exec,rm -rf $(call quote,$(__install_dir)))\
	$(call, ### Move from the temporary prefix to the proper one. Note the janky abspath, which is needed because of how DESTDIR works.)\
	$(call safe_shell_exec,mv $(call quote,$(__source_dir)/__tmp_prefix/$(abspath $(__install_dir))) $(call quote,$(__install_dir)))\
	$(call safe_shell_exec,rm -rf $(call quote,$(__source_dir)/__tmp_prefix))\
