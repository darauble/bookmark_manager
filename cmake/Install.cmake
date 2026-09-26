# =============================================================================
# Install.cmake - install the built plugin into an existing SDR++ installation.
#
# This is only used in STANDALONE mode. It tries to locate an already-installed
# SDR++ on the system and computes the correct plugin directory, so that after
#     cmake --build .
#     cmake --install .   (or `make install`)
# the plugin lands where the installed SDR++ will pick it up.
#
# Detection order for the plugin directory:
#   1) User override: -DSDRPP_PLUGIN_DIR=/path
#   2) Platform defaults, validated against a detected SDR++ install
#   3) Fallback to <CMAKE_INSTALL_PREFIX>/lib/sdrpp/plugins with a warning
# =============================================================================

include(GNUInstallDirs)

# ---- 1) Explicit override ---------------------------------------------------
if (SDRPP_PLUGIN_DIR)
    message(STATUS "Bookmark Manager: using user-specified plugin dir: ${SDRPP_PLUGIN_DIR}")
    set(_BM_PLUGIN_DIR "${SDRPP_PLUGIN_DIR}")
else ()
    # ---- 2) Platform-specific detection of an installed SDR++ ---------------
    if (WIN32)
        # On Windows, plugins live in <sdrpp_dir>/modules next to sdrpp.exe.
        # Try to locate the SDR++ executable.
        find_program(SDRPP_EXECUTABLE
            NAMES sdrpp sdrpp.exe
            PATHS
                "$ENV{ProgramFiles}/SDR++"
                "$ENV{ProgramFiles\(x86\)}/SDR++"
                "C:/Program Files/SDR++"
            DOC "Path to the installed SDR++ executable")
        if (SDRPP_EXECUTABLE)
            get_filename_component(_SDRPP_BIN_DIR "${SDRPP_EXECUTABLE}" DIRECTORY)
            set(_BM_PLUGIN_DIR "${_SDRPP_BIN_DIR}/modules")
        endif ()

    elseif (APPLE)
        # On macOS SDR++ ships as an .app bundle; plugins go under
        # SDR++.app/Contents/Plugins.
        foreach (_appdir "/Applications/SDR++.app" "$ENV{HOME}/Applications/SDR++.app")
            if (EXISTS "${_appdir}/Contents/MacOS/sdrpp")
                set(_BM_PLUGIN_DIR "${_appdir}/Contents/Plugins")
                break ()
            endif ()
        endforeach ()

    else ()
        # Linux / *BSD: SDR++ hardcodes its plugin dir as
        # INSTALL_PREFIX "/lib/sdrpp/plugins" (always plain "lib", never the
        # multiarch libdir). Probe common prefixes for an installed SDR++.
        foreach (_prefix "/usr" "/usr/local")
            if (EXISTS "${_prefix}/bin/sdrpp"
                OR EXISTS "${_prefix}/lib/sdrpp"
                OR EXISTS "${_prefix}/lib/sdrpp/plugins")
                set(_BM_PLUGIN_DIR "${_prefix}/lib/sdrpp/plugins")
                break ()
            endif ()
        endforeach ()
    endif ()
endif ()

# ---- 3) Fallback ------------------------------------------------------------
if (NOT _BM_PLUGIN_DIR)
    if (WIN32)
        set(_BM_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/modules")
    elseif (APPLE)
        set(_BM_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/lib/sdrpp/plugins")
    else ()
        set(_BM_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/lib/sdrpp/plugins")
    endif ()
    message(WARNING
        "Bookmark Manager: could not detect an installed SDR++.\n"
        "Falling back to plugin dir: ${_BM_PLUGIN_DIR}\n"
        "If SDR++ is installed elsewhere, re-run cmake with "
        "-DSDRPP_PLUGIN_DIR=/path/to/sdrpp/plugins")
else ()
    message(STATUS "Bookmark Manager: plugin will be installed to: ${_BM_PLUGIN_DIR}")
endif ()

# Expose for packaging.
set(BOOKMARK_MANAGER_PLUGIN_DIR "${_BM_PLUGIN_DIR}" CACHE PATH "SDR++ plugin install directory" FORCE)

# When building a distributable package (DEB/RPM/archive) we must NOT bake an
# absolute machine-specific detected path into the package. Instead we install
# to a standard prefix-relative location that matches where SDR++ packages put
# their plugins. `BOOKMARK_MANAGER_PACKAGING` is set ON by Packaging.cmake/CPack.
if (BOOKMARK_MANAGER_PACKAGING)
    if (WIN32)
        set(_BM_INSTALL_DEST "modules")
    else ()
        # SDR++ looks in INSTALL_PREFIX/lib/sdrpp/plugins (fixed "lib", not the
        # distro multiarch libdir), so the package must install there too.
        set(_BM_INSTALL_DEST "lib/sdrpp/plugins")
    endif ()
    message(STATUS "Bookmark Manager: packaging install destination: ${_BM_INSTALL_DEST}")
else ()
    set(_BM_INSTALL_DEST "${_BM_PLUGIN_DIR}")
endif ()

# Install the plugin. RUNTIME covers Windows .dll, LIBRARY covers .so/.dylib.
# Use a dedicated component so packaging can ship ONLY the plugin (not the
# sdrpp_core library, which is provided by the already-installed SDR++).
install(TARGETS bookmark_manager
    RUNTIME DESTINATION "${_BM_INSTALL_DEST}" COMPONENT plugin
    LIBRARY DESTINATION "${_BM_INSTALL_DEST}" COMPONENT plugin)
