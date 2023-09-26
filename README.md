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

## Compiling

Checkout the [SDR++](https://github.com/AlexandreRouma/SDRPlusPlus). Then checkout **bookmark_manager** into the **misc_modules** directory.

In SDR++'s **CMakeList.txt** file add the following lines

```
option(OPT_BUILD_BOOKMARK_MANAGER "Build the Bookmark Manager module" ON)
```

and

```
if (OPT_BUILD_BOOKMARK_MANAGER)
add_subdirectory("misc_modules/bookmark_manager")
endif (OPT_BUILD_BOOKMARK_MANAGER)
```

where appropriate.

Then compile all SDR++, `make install`, run it, add the Bookmarks Manager into your panel using Module Manager.

To migrate old bookmarks simply copy `frequency_manager.json` and rename it to `bookmark_manager.json`, then adjust the line number to a desired one.

