# Changelog

* [1.6.2](#1-6-2)
* [1.6.1](#1-6-1)
* [1.6.0](#1-6-0)
* [1.5.0](#1-5-0)


## 1.6.2

### Added

* Text shaping support.
* Support for middle and right mouse buttons, mouse wheel and trackpad
  scrolling (https://codeberg.org/dnkl/yambar/issues/39).
* script: polling mode. See the new `poll-interval` option
  (https://codeberg.org/dnkl/yambar/issues/67).


### Changed

* doc: split up **yambar-modules**(5) into multiple man pages, one for
  each module (https://codeberg.org/dnkl/yambar/issues/15).
* fcft >= 2.4.0 is now required.
* sway-xkb: non-keyboard inputs are now ignored
  (https://codeberg.org/dnkl/yambar/issues/51).
* battery: don’t terminate (causing last status to “freeze”) when
  failing to update; retry again later
  (https://codeberg.org/dnkl/yambar/issues/44).
* battery: differentiate "Not Charging" and "Discharging" in state
  tag of battery module.
  (https://codeberg.org/dnkl/yambar/issues/57).
* string: use HORIZONTAL ELLIPSIS instead of three regular periods
  when truncating a string
  (https://codeberg.org/dnkl/yambar/issues/73).


### Fixed

* Crash when merging non-dictionary anchors in the YAML configuration
  (https://codeberg.org/dnkl/yambar/issues/32).
* Crash in the `ramp` particle when the tag’s value was out-of-bounds
  (https://codeberg.org/dnkl/yambar/issues/45).
* Crash when a string particle contained `{}`
  (https://codeberg.org/dnkl/yambar/issues/48).
* `script` module rejecting range tag end values containing the digit
  `9` (https://codeberg.org/dnkl/yambar/issues/60).


### Contributors

* [novakane](https://codeberg.org/novakane)
* [mz](https://codeberg.org/mz)


## 1.6.1

### Changed

* i3: workspaces with numerical names are sorted separately from
  non-numerically named workspaces
  (https://codeberg.org/dnkl/yambar/issues/30).


### Fixed

* mpd: `elapsed` tag not working (regression, introduced in 1.6.0).
* Wrong background color for (semi-) transparent backgrounds.
* battery: stats sometimes getting stuck at 0, or impossibly large
  values (https://codeberg.org/dnkl/yambar/issues/25).


## 1.6.0

### Added

* alsa: `percent` tag. This is an integer tag that represents the
  current volume as a percentage value
  (https://codeberg.org/dnkl/yambar/issues/10).
* river: added documentation
  (https://codeberg.org/dnkl/yambar/issues/9).
* script: new module, adds support for custom user scripts
  (https://codeberg.org/dnkl/yambar/issues/11).
* mpd: `volume` tag. This is a range tag that represents MPD's current
  volume in percentage (0-100)
* i3: `sort` configuration option, that controls how the workspace
  list is sorted. Can be set to one of `none`, `ascending` or
  `descending`. Default is `none`
  (https://codeberg.org/dnkl/yambar/issues/17).
* i3: `mode` tag: the name of the currently active mode


### Fixed

* YAML parsing error messages being replaced with a generic _“unknown
  error”_.
* Memory leak when a YAML parsing error was encountered.
* clock: update every second when necessary
  (https://codeberg.org/dnkl/yambar/issues/12).
* mpd: fix compilation with clang
  (https://codeberg.org/dnkl/yambar/issues/16).
* Crash when the alpha component in a color value was 0.
* XCB: Fallback to non-primary monitor when the primary monitor is
  disconnected (https://codeberg.org/dnkl/yambar/issues/20)


### Contributors

* [JorwLNKwpH](https://codeberg.org/JorwLNKwpH)
* [optimus-prime](https://codeberg.org/optimus-prime)


## 1.5.0

### Added

* battery: support for drivers that use `charge_*` (instead of
  `energy_*`) sys files.
* removables: SD card support.
* removables: new `ignore` property.
* Wayland: multi-seat support.
* **Experimental**: 'river': new module for the river Wayland compositor.


### Changed

* Requires fcft-2.2.x.
* battery: a poll value of 0 disables polling.


### Fixed

* mpd: check of return value from `thrd_create`.
* battery: handle 'manufacturer' and 'model_name' not being present.
* Wayland: handle runtime scaling changes.
