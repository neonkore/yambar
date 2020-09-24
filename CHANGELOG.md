# Changelog

* [Unreleased](#unreleased)
* [1.5.0](#1-5-0)


## Unreleased
### Added
### Deprecated
### Removed
### Fixed

* YAML parsing error messages being replaced with a generic “unknown error”.
* Memory leak when a YAML parsing error was encoutered.


### Security
### Contributors


## 1.5.0

### Added

* battery: support for drivers that use 'charge\_\*' (instead of
  'energy\_\*') sys files.
* removables: SD card support.
* removables: new 'ignore' property.
* Wayland: multi-seat support.
* **Experimental**: 'river': new module for the river Wayland compositor.


### Changed

* Requires fcft-2.2.x.
* battery: a poll value of 0 disables polling.


### Fixed

* mpd: check of return value from `thrd_create`.
* battery: handle 'manufacturer' and 'model_name' not being present.
* Wayland: handle runtime scaling changes.
