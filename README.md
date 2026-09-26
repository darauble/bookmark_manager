# bookmark_manager

An alternative bookmark manager for [SDR++](https://github.com/AlexandreRouma/SDRPlusPlus).

The original Frequency Manager included with the SDR++ has an inconvenience of arranging bookmarks in one row only, so they get overlapped:

![Overlapping bookmarks in SDR++ Frequency Manager](screenshots/sdrpp-overlapped-bookmarks.png?raw=true "Overlapping bookmarks in SDR++ Frequency Manager")

I really like SDR++ for its cleanliness, but this issue bugged me a lot. So I simply took the source of the Frequency Manager, copied it, renamed to Bookmark Manager and "fixed" it for myself.

Users can now choose to use between 1 and 10 lines for bookmarks to be automatically arranged:

![Bookmark Manager arranges bookmarks in several rows](screenshots/sdrpp-bookmark-manager.png?raw=true "Bookmark Manager arranges bookmarks in several rows")

Bookmarks with UTC, appropriate week days and currently not online are greyed out.

Additionally, by marking "Bookmark Rectangle" to off bookmarks are displayed in text-only, without fat rectangle around:

![Bookmark Manager arranges bookmarks in several rows](screenshots/sdrpp-bookmark-manager-text.png?raw=true "Bookmark Manager arranges bookmarks in several rows")

And each list can be colored to easier distinguish between stations, types or whatever works for the user.


## Current Features

(As opposed to original Frequency Manager)

* Bookmark arrangement in up to 10 (user chosen) rows, both top and bottom arrangements
* Bookmarks can be displayed in text-only, without fat rectangle
* Cashed position for mouse-over (no recalculation)
* UTC start/end times of the broadcast (leave 0000 in both for all day broadcasts)
* Week days for a bookmark (all checked by default)
* Each list can be assigned an individual color

Features introduced by Davide Rovelli:
* Labels centered or on the side (flag like)
* Limit clutter to last row and stopping clutter by skipping too many bookmarks
* Clicking on bookmark also selects it in the manager list
* Additional data fields for geoinfo and personal notes

## Planned Features

I also have other plans for Bookmark Manager in the future:

* Add a toggle to show/hide bookmarks that are not on time

## Building

Bookmark Manager can be built in **two ways**:

1. **Standalone** (recommended) — build just this plugin against the SDR++
   *core* (pulled in as a git submodule). SDR++ itself is **not** built in full;
   only `sdrpp_core` is compiled as a build-time dependency. The result is a
   single loadable plugin that an already-installed SDR++ picks up.
2. **In-tree** — the classic way, as a module inside the SDR++ source tree.

### 1. Standalone build (SDR++ as a submodule)

SDR++ is referenced as a git submodule pinned to a tested commit, so no changes
to the SDR++ tree are needed.

#### Get the sources

```sh
git clone <this-repo-url> bookmark_manager
cd bookmark_manager
git submodule update --init --recursive
```

The SDR++ source is checked out under `vendor/SDRPlusPlus` at a known-good
commit. The build only compiles `sdrpp_core` from it — not the whole app.

#### Install build dependencies

These are the dependencies required to build SDR++ core. **Building does not
require SDR++ to be installed** — only *installing the plugin* does (see below).

* **Debian/Ubuntu**
  ```sh
  sudo apt install build-essential cmake pkg-config \
      libfftw3-dev libglfw3-dev libvolk2-dev libzstd-dev \
      libglew-dev libgl1-mesa-dev
  # for packaging: also install `rpm`
  ```
* **Fedora/RHEL**
  ```sh
  sudo dnf install gcc-c++ cmake pkgconf-pkg-config \
      fftw-devel glfw-devel volk-devel libzstd-devel \
      glew-devel mesa-libGL-devel rpm-build
  ```
* **macOS** (Homebrew) — `brew install cmake pkg-config fftw glfw zstd`, and
  build [volk](https://github.com/gnuradio/volk) from source.
* **Windows** — dependencies via [vcpkg](https://vcpkg.io)
  (`fftw3`, `glfw3`, `zstd`) plus `volk` from
  [PothosSDR](https://downloads.myriadrf.org/builds/PothosSDR/).

#### Configure & build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bookmark_manager -j$(nproc)
```

This produces the plugin:

* Linux:   `build/bookmark_manager.so`
* macOS:   `build/bookmark_manager.dylib`
* Windows: `build/Release/bookmark_manager.dll`

#### Install into an existing SDR++

`make install` (or `cmake --install`) detects where SDR++ is installed on the
system and copies the plugin into the correct plugin directory:

* Linux:   `<prefix>/lib/sdrpp/plugins` (e.g. `/usr/lib/sdrpp/plugins`)
* macOS:   `SDR++.app/Contents/Plugins`
* Windows: `<SDR++ install dir>/modules`

```sh
sudo cmake --install build
```

If SDR++ is installed in a non-standard location, point the install at the
right directory explicitly:

```sh
cmake -S . -B build -DSDRPP_PLUGIN_DIR=/path/to/sdrpp/plugins
sudo cmake --install build
```

After installing, launch SDR++ and enable **Bookmark Manager** via the Module
Manager.

#### Building installable packages

Enable packaging and run CPack:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DBOOKMARK_MANAGER_ENABLE_PACKAGING=ON -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --target bookmark_manager -j$(nproc)
cd build && cpack
```

* **Linux** — produces a `.deb` and a `.rpm`. Both declare a dependency on
  `sdrpp` and install only the plugin into `/usr/lib/sdrpp/plugins`.
  ```sh
  sudo apt install ./sdrpp-bookmark-manager_*_Linux_x86_64.deb
  # or
  sudo dnf install ./sdrpp-bookmark-manager_*_Linux_x86_64.rpm
  ```
* **macOS** — produces a `.tar.gz` archive containing the `.dylib`.
* **Windows** — produces a `.zip` archive containing the `.dll`.

Prebuilt packages for Linux, macOS and Windows are produced automatically by the
GitHub Actions workflow (`.github/workflows/build.yml`) and attached to tagged
releases.

### 2. In-tree build (classic SDR++ module)

Checkout [SDR++](https://github.com/AlexandreRouma/SDRPlusPlus), then place
**bookmark_manager** into the **misc_modules** directory.

In SDR++'s **CMakeLists.txt** add:

```cmake
option(OPT_BUILD_BOOKMARK_MANAGER "Build the Bookmark Manager module" ON)
```

and

```cmake
if (OPT_BUILD_BOOKMARK_MANAGER)
add_subdirectory("misc_modules/bookmark_manager")
endif (OPT_BUILD_BOOKMARK_MANAGER)
```

where appropriate. Then compile all of SDR++, `make install`, run it, and add
Bookmark Manager to your panel using the Module Manager.

The same `CMakeLists.txt` supports both modes automatically: when built inside
the SDR++ tree it detects the existing `sdrpp_core` target and behaves like the
original module; standalone it builds `sdrpp_core` from the submodule.

## Migrating old bookmarks

To migrate old bookmarks simply copy `frequency_manager.json`, rename it to
`bookmark_manager.json`, then adjust the line number to a desired one.





