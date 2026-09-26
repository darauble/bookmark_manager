# =============================================================================
# Packaging.cmake - CPack configuration for the standalone plugin.
#
# Produces:
#   - Linux : .deb (CPackDeb) and .rpm (CPackRPM)
#   - macOS : .tar.gz archive (TGZ) containing the .dylib
#   - Windows: .zip archive containing the .dll
#
# The packages install a single plugin shared object into SDR++'s plugin
# directory. They declare a dependency on SDR++ so package managers can flag a
# missing base install (best-effort; SDR++ package naming varies by distro).
#
# Enable with: -DBOOKMARK_MANAGER_ENABLE_PACKAGING=ON
# Then:        cpack   (from the build dir)
# =============================================================================

option(BOOKMARK_MANAGER_ENABLE_PACKAGING "Enable CPack packaging targets" OFF)

if (NOT BOOKMARK_MANAGER_ENABLE_PACKAGING)
    return ()
endif ()

# Signal to Install.cmake to use prefix-relative, package-friendly destinations.
# (Install.cmake is included before this file, so re-run its install logic with
# packaging semantics by setting the flag and re-including is not possible here.
# Instead we require the flag to be set at configure time - see CMakeLists.txt.)
if (NOT BOOKMARK_MANAGER_PACKAGING)
    message(WARNING
        "BOOKMARK_MANAGER_ENABLE_PACKAGING is ON but BOOKMARK_MANAGER_PACKAGING "
        "was not set before install rules were evaluated. Configure with "
        "-DBOOKMARK_MANAGER_PACKAGING=ON to ensure package-relative install paths.")
endif ()

set(CPACK_PACKAGE_NAME "sdrpp-bookmark-manager")
set(CPACK_PACKAGE_VENDOR "Darau Ble")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Alternative bookmark manager plugin for SDR++")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT "Darau Ble <noreply@example.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "SDR++ Bookmark Manager")
set(CPACK_STRIP_FILES ON)

# Package ONLY the plugin component. The sdrpp_core library that gets built
# from the submodule (and its own install rule) must NOT be shipped: the target
# system already has SDR++ with its own libsdrpp_core. Restricting CPack to the
# `plugin` component ensures the package contains just bookmark_manager.
set(CPACK_COMPONENTS_ALL plugin)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
# Don't append the component name to the archive/package file name.
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)

# Use a clean architecture-tagged file name.
set(CPACK_PACKAGE_FILE_NAME
    "sdrpp-bookmark-manager_${PROJECT_VERSION}_${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}")

if (WIN32)
    # -------- Windows: zip archive --------
    set(CPACK_GENERATOR "ZIP")

elseif (APPLE)
    # -------- macOS: tar.gz archive --------
    set(CPACK_GENERATOR "TGZ")

else ()
    # -------- Linux: DEB + RPM --------
    set(CPACK_GENERATOR "DEB;RPM")

    # ---- Debian ----
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/AlexandreRouma/SDRPlusPlus")
    # Depend on SDR++ (package name is 'sdrpp' in the upstream .deb).
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "sdrpp")
    # Auto-detect shared library dependencies (fftw, glfw, volk, zstd, ...).
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # Normalize the architecture field (e.g. amd64).
    if (NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
        execute_process(COMMAND dpkg --print-architecture
            OUTPUT_VARIABLE _DEB_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if (_DEB_ARCH)
            set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_DEB_ARCH}")
        endif ()
    endif ()

    # ---- RPM ----
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Communications")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/AlexandreRouma/SDRPlusPlus")
    set(CPACK_RPM_PACKAGE_REQUIRES "sdrpp")
    # Don't let RPM claim ownership of directories provided by SDR++.
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        "/usr/lib/sdrpp" "/usr/lib64/sdrpp" "${CMAKE_INSTALL_PREFIX}")
endif ()

include(CPack)
