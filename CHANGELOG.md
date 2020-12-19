# Changelog

* [Unreleased](#Unreleased)
* [1.6.0](#1-6-0)
* [1.5.0](#1-5-0)


## Unreleased

### Added
### Changed
### Deprecated
### Removed
### Fixed
### Security


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
