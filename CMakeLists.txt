cmake_minimum_required(VERSION 3.13)
project(bookmark_manager VERSION 0.1.8 LANGUAGES C CXX)

# =============================================================================
# SDR++ Bookmark Manager - dual-mode build
#
#   1) IN-TREE MODE
#      Used when this directory is built as part of the SDR++ source tree
#      (i.e. added via `add_subdirectory("misc_modules/bookmark_manager")`
#      from the SDR++ root CMakeLists.txt). In that case the `sdrpp_core`
#      target and all module helper variables already exist, so we behave
#      exactly like the original module.
#
#   2) STANDALONE MODE
#      Used when this directory is configured on its own. SDR++ is pulled in
#      as a git submodule (vendor/SDRPlusPlus). We only add the SDR++ *core*
#      as a dependency (`add_subdirectory(.../core)`) - the full SDR++
#      application and all its other modules are NOT built. The result is a
#      single loadable plugin (.so/.dll/.dylib) that an already-installed
#      SDR++ can pick up.
# =============================================================================

# The plugin sources
file(GLOB SRC "src/*.cpp")

# Detect which mode we are in: if `sdrpp_core` is already a known target we are
# being built from within the SDR++ tree.
if (TARGET sdrpp_core)
    set(BOOKMARK_MANAGER_INTREE TRUE)
else ()
    set(BOOKMARK_MANAGER_INTREE FALSE)
endif ()

# -----------------------------------------------------------------------------
# STANDALONE MODE: build only sdrpp_core from the submodule as a dependency
# -----------------------------------------------------------------------------
if (NOT BOOKMARK_MANAGER_INTREE)
    # Location of the vendored SDR++ checkout (git submodule).
    set(SDRPP_SUBMODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDRPlusPlus"
        CACHE PATH "Path to the SDR++ source tree (git submodule)")

    if (NOT EXISTS "${SDRPP_SUBMODULE_DIR}/core/CMakeLists.txt")
        message(FATAL_ERROR
            "SDR++ submodule not found at ${SDRPP_SUBMODULE_DIR}.\n"
            "Initialize it with:\n"
            "    git submodule update --init --recursive")
    endif ()

    # ---- Options mirrored from the SDR++ root CMakeLists.txt --------------
    # These are consumed by core/CMakeLists.txt. We only need the ones that
    # affect how core is built.
    option(OPT_BACKEND_GLFW "Use the GLFW backend" ON)
    option(OPT_BACKEND_ANDROID "Use the Android backend" OFF)
    option(OPT_OVERRIDE_STD_FILESYSTEM "Use a local version of std::filesystem" OFF)
    option(USE_INTERNAL_LIBCORRECT "Use an internal version of libcorrect" ON)
    option(USE_BUNDLE_DEFAULTS "Use MacOS .app bundle default dirs" OFF)

    # Default build type
    if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif ()

    # ---- Compiler flags (mirrored from SDR++ root) -----------------------
    if (CMAKE_BUILD_TYPE MATCHES "Debug")
        if (MSVC)
            set(SDRPP_COMPILER_FLAGS /std:c++17 /EHsc)
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(SDRPP_COMPILER_FLAGS -g -Og -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
        else ()
            set(SDRPP_COMPILER_FLAGS -g -Og -std=c++17)
        endif ()
    else ()
        if (MSVC)
            set(SDRPP_COMPILER_FLAGS /O2 /Ob2 /std:c++17 /EHsc)
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(SDRPP_COMPILER_FLAGS -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
        else ()
            set(SDRPP_COMPILER_FLAGS -O3 -std=c++17)
        endif ()
    endif ()
    set(SDRPP_MODULE_COMPILER_FLAGS ${SDRPP_COMPILER_FLAGS})

    # Root of the SDR++ core sources (used for include dirs below).
    set(SDRPP_CORE_ROOT "${SDRPP_SUBMODULE_DIR}/core")

    # Build ONLY sdrpp_core from the submodule (not the whole application).
    # The binary dir is kept separate to avoid polluting our tree.
    #
    # EXCLUDE_FROM_ALL keeps core out of the default `all` target's install set:
    # core registers its own `install(TARGETS sdrpp_core ...)` rule, but we do
    # NOT want to ship/overwrite that library (the target system already has
    # SDR++). With EXCLUDE_FROM_ALL, a plain `make install` installs only our
    # plugin. Packaging additionally restricts to COMPONENT `plugin`
    # (see cmake/Install.cmake and cmake/Packaging.cmake). sdrpp_core is still
    # built on demand because our plugin links against it.
    add_subdirectory("${SDRPP_SUBMODULE_DIR}/core" "${CMAKE_BINARY_DIR}/sdrpp_core" EXCLUDE_FROM_ALL)
endif ()

# -----------------------------------------------------------------------------
# The plugin target (common to both modes)
# -----------------------------------------------------------------------------
add_library(bookmark_manager SHARED ${SRC})
target_link_libraries(bookmark_manager PRIVATE sdrpp_core)
set_target_properties(bookmark_manager PROPERTIES PREFIX "")

# Include dirs: own sources, plus the radio decoder headers (radio_interface.h).
if (BOOKMARK_MANAGER_INTREE)
    # Paths relative to the in-tree location misc_modules/bookmark_manager
    target_include_directories(bookmark_manager PRIVATE
        "src/"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../decoder_modules/radio/src")
else ()
    target_include_directories(bookmark_manager PRIVATE
        "src/"
        "${SDRPP_SUBMODULE_DIR}/core/src"
        "${SDRPP_SUBMODULE_DIR}/decoder_modules/radio/src")
endif ()

# Compiler options
if (MSVC)
    target_compile_options(bookmark_manager PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(bookmark_manager PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(bookmark_manager PRIVATE -O3 -std=c++17)
endif ()

# -----------------------------------------------------------------------------
# Install + packaging (standalone mode only; in-tree the SDR++ tree handles it)
# -----------------------------------------------------------------------------
if (BOOKMARK_MANAGER_INTREE)
    # Match the original in-tree install directive.
    install(TARGETS bookmark_manager DESTINATION lib/sdrpp/plugins)
else ()
    # When packaging is requested, use prefix-relative install paths so the
    # generated DEB/RPM/archive is portable (no machine-specific absolute path
    # baked in). This must be decided before Install.cmake runs its install().
    option(BOOKMARK_MANAGER_ENABLE_PACKAGING "Enable CPack packaging targets" OFF)
    if (BOOKMARK_MANAGER_ENABLE_PACKAGING)
        set(BOOKMARK_MANAGER_PACKAGING ON)
    endif ()

    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Install.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake")
endif ()
