# MUI Version Changelog

## 1.3
* Split the big mui.h header into smaller headers. It was just getting too big really.
* Removed *incbin.h* -- this was incompatible with several linkers, and didn't work for webassemly. Now the fonts are just included as C arrays.
* Introduced a new *mui_control_group*. Basically the list of control is now made of (optional) multiple groups. A normal window only uses one, but for tab controls for example, that'll be quite handy.
* This introduces a new API to iterate the window/control list, you can selectively iterate all, or just the visible controls.
* There is now a standard PUT file dialog, with a "New" folder button and all.
* Added an optional close box to the window title bars.

## 1.2
* More tweaks to the menus. Popup menus can be justified left/right/center. Removed quite a few fudge factors.
* Added a notion of a control (per window) having the 'focus'. Currently listboxes and text edit boxes can have the focus.

## 1.1 -- since the release of the original version
* Added support for horizontal scrollbars.
* Added a faster (vectorized) version of an obvious 'cg' function.
* Fixed a problem with mui_timers. Also typed it now.
* Added a bunch of extra comments to mui.h
* Added a *fully justified* text style for text boxes.

## 1.0 -- original version
* Initial release. Doh.
