# Changelog

* [Unreleased](#unreleased)
* [1.9.0](#1-9-0)
* [1.8.0](#1-8-0)
* [1.7.0](#1-7-0)
* [1.6.2](#1-6-2)
* [1.6.1](#1-6-1)
* [1.6.0](#1-6-0)
* [1.5.0](#1-5-0)


## Unreleased
### Added

* Field width tag format option ([#246][246])
* river: support for ‘layout’ events.
* dwl: support for specifying name of tags ([#256][256])
* i3/sway: extend option `sort`; use `native` to sort numbered workspaces only.
* modules/dwl: handle the appid status ([#284][284])

[246]: https://codeberg.org/dnkl/yambar/issues/246
[256]: https://codeberg.org/dnkl/yambar/pulls/256
[284]: https://codeberg.org/dnkl/yambar/pulls/284


### Changed

* disk-io: `interval` renamed to `poll-interval`
* mem: `interval` renamed to `poll-interval`
* battery/network/script: `poll-interval` unit changed from seconds to
  milliseconds ([#244][244]).
* all modules: minimum poll interval changed from 500ms to 250ms.
* network: do not use IPv6 link-local ([#281][281])

[244]: https://codeberg.org/dnkl/yambar/issues/244
[281]: https://codeberg.org/dnkl/yambar/pulls/281


### Deprecated
### Removed
### Fixed

* Build failures for certain combinations of enabled and disabled
  plugins ([#239][239]).
* Documentation for the `cpu` module; `interval` has been renamed to
  `poll-interval` ([#241][241]).
* battery: module was not thread safe.
* dwl module reporting only the last part of the title ([#251][251])
* i3/sway: regression; persistent workspaces shown twice
  ([#253][253]).
* pipewire: use roundf instead of ceilf for more accuracy ([#262][262])
* Crash when a yaml anchor has a value to already exists in the target
  yaml node ([#286][286]).
* battery: Fix time conversion in battery estimation ([#303][303]).

[239]: https://codeberg.org/dnkl/yambar/issues/239
[241]: https://codeberg.org/dnkl/yambar/issues/241
[251]: https://codeberg.org/dnkl/yambar/pulls/251
[253]: https://codeberg.org/dnkl/yambar/issues/253
[262]: https://codeberg.org/dnkl/yambar/issues/262
[286]: https://codeberg.org/dnkl/yambar/issues/286


### Security
### Contributors

* Leonardo Gibrowski Faé (Horus)

## 1.9.0

### Added

* Support for specifying number of decimals when printing a float tag
  ([#200][200]).
* Support for custom font fallbacks ([#153][153]).
* overline: new decoration ([#153][153]).
* i3/sway: boolean option `strip-workspace-numbers`.
* font-shaping: new inheritable configuration option, allowing you to
  configure whether strings should be _shaped_ using HarfBuzz, or not
  ([#159][159]).
* river: support for the new “mode” event present in version 3 of the
  river status manager protocol, in the form of a new tag, _”mode”_,
  in the `title` particle.
* network: request link stats and expose under tags `dl-speed` and
  `ul-speed` when `poll-interval` is set.
* new module: disk-io.
* new module: pulse ([#223][223]).
* alsa: `dB` tag ([#202][202]).
* mpd: `file` tag ([#219][219]).
* pipewire: add a new module for pipewire ([#224][224])
* on-click: support `next`/`previous` mouse buttons ([#228][228]).
* dwl: add a new module for DWL ([#218][218])
* sway: support for workspace ‘rename’ and ‘move’ events
  ([#216][216]).

[153]: https://codeberg.org/dnkl/yambar/issues/153
[159]: https://codeberg.org/dnkl/yambar/issues/159
[200]: https://codeberg.org/dnkl/yambar/issues/200
[202]: https://codeberg.org/dnkl/yambar/issues/202
[218]: https://codeberg.org/dnkl/yambar/pulls/218
[219]: https://codeberg.org/dnkl/yambar/pulls/219
[223]: https://codeberg.org/dnkl/yambar/pulls/223
[224]: https://codeberg.org/dnkl/yambar/pulls/224
[228]: https://codeberg.org/dnkl/yambar/pulls/228
[216]: https://codeberg.org/dnkl/yambar/issues/216


### Changed

* All modules are now compile-time optional.
* Minimum required meson version is now 0.59.
* Float tags are now treated as floats instead of integers when
  formatted with the `kb`/`kib`/`mb`/`mib`/`gb`/`gib` string particle
  formatters.
* network: `tx-bitrate` and `rx-bitrate` are now in bits/s instead of
  Mb/s. Use the `mb` string formatter to render these tags as before
  (e.g. `string: {text: "{tx-bitrate:mb}"}`).
* i3: newly created, and **unfocused** workspaces are now considered
  non-empty ([#191][191])
* alsa: use dB instead of raw volume values, if possible, when
  calculating the `percent` tag ([#202][202])
* cpu: `content` particle is now a template instantiated once for each
  core, and once for the total CPU usage. See
  **yambar-modules-cpu**(5) for more information ([#207][207]).
* **BREAKING CHANGE**: overhaul of the `map` particle. Instead of
  specifying a `tag` and then an array of `values`, you must now
  simply use an array of `conditions`, that consist of:

  `<tag> <operation> <value>`

  where `<operation>` is one of:

  `== != < <= > >=`

  Note that boolean tags must be used as is:

  `online`

  `~online # use '~' to match for their falsehood`

  As an example, if you previously had something like:

  ```
  map:
    tag: State
    values:
      unrecognized:
        ...
  ```

  You would now write it as:

  ```
  map:
    conditions:
      State == unrecognized:
        ...
  ```

  Note that if `<value>` contains any non-alphanumerical characters,
  it **must** be surrounded by `""`:

  `State == "very confused!!!"`

  Finally, you can mix and match conditions using the boolean
  operators `&&` and `||`:

  ```
  <condition1> && <condition2>
  <condition1> && (<condition2> || <condition3>) # parenthesis work
  ~(<condition1> && <condition2>) # '~' can be applied to any condition
  ```

  For a more thorough explanation, see the updated map section in the
  man page for yambar-particles([#137][137], [#175][175] and [#][182]).

[137]: https://codeberg.org/dnkl/yambar/issues/137
[175]: https://codeberg.org/dnkl/yambar/issues/172
[182]: https://codeberg.org/dnkl/yambar/issues/182
[191]: https://codeberg.org/dnkl/yambar/issues/191
[202]: https://codeberg.org/dnkl/yambar/issues/202
[207]: https://codeberg.org/dnkl/yambar/issues/207


### Fixed

* i3: fixed “missing workspace indicator” (_err: modules/i3.c:94:
  workspace reply/event without 'name' and/or 'output', and/or 'focus'
  properties_).
* Slow/laggy behavior when quickly spawning many `on-click` handlers,
  e.g. when handling mouse wheel events ([#169][169]).
* cpu: don’t error out on systems where SMT has been disabled
  ([#172][172]).
* examples/dwl-tags: updated parsing of `output` name ([#178][178]).
* sway-xkb: don’t crash when Sway sends an _”added”_ event for a
  device yambar is already tracking ([#177][177]).
* Crash when a particle is “too wide”, and tries to render outside the
  bar ([#198][198]).
* string: crash when failing to convert string to UTF-32.
* script: only first transaction processed when receiving multiple
  transactions in a single batch ([#221][221]).
* network: missing SSID (recent kernels, or possibly wireless drivers,
  no longer provide the SSID in the `NL80211_CMD_NEW_STATION`
  response) ([#226][226]).
* sway-xkb: crash when compositor presents multiple inputs with
  identical IDs ([#229][229]).

[169]: https://codeberg.org/dnkl/yambar/issues/169
[172]: https://codeberg.org/dnkl/yambar/issues/172
[178]: https://codeberg.org/dnkl/yambar/issues/178
[177]: https://codeberg.org/dnkl/yambar/issues/177
[198]: https://codeberg.org/dnkl/yambar/issues/198
[221]: https://codeberg.org/dnkl/yambar/issues/221
[226]: https://codeberg.org/dnkl/yambar/issues/226
[229]: https://codeberg.org/dnkl/yambar/issues/229


### Contributors

* Baptiste Daroussin
* Horus
* Johannes
* Leonardo Gibrowski Faé
* Leonardo Neumann
* Midgard
* Ogromny
* Peter Rice
* Timur Celik
* Willem van de Krol
* hiog


## 1.8.0

### Added

* ramp: can now have custom min and max values
  ([#103](https://codeberg.org/dnkl/yambar/issues/103)).
* border: new decoration.
* i3/sway: new boolean tag: `empty`
  ([#139](https://codeberg.org/dnkl/yambar/issues/139)).
* mem: a module handling system memory monitoring
* cpu: a module offering cpu usage monitoring
* removables: support for audio CDs
  ([#146](https://codeberg.org/dnkl/yambar/issues/146)).
* removables: new boolean tag: `audio`.


### Changed

* fcft >= 3.0 is now required.
* Made `libmpdclient` an optional dependency
* battery: unknown battery states are now mapped to ‘unknown’, instead
  of ‘discharging’.
* Wayland: the bar no longer exits when the monitor is
  disabled/unplugged ([#106](https://codeberg.org/dnkl/yambar/issues/106)).


### Fixed

* `left-margin` and `right-margin` from being rejected as invalid
  options.
* Crash when `udev_monitor_receive_device()` returned `NULL`. This
  affected the “backlight”, “battery” and “removables” modules
  ([#109](https://codeberg.org/dnkl/yambar/issues/109)).
* foreign-toplevel: update bar when a top-level is closed.
* Bar not being mapped on an output before at least one module has
  “refreshed” it ([#116](https://codeberg.org/dnkl/yambar/issues/116)).
* network: failure to retrieve wireless attributes (SSID, RX/TX
  bitrate, signal strength etc).
* Integer options that were supposed to be >= 0 were incorrectly
  allowed, leading to various bad things; including yambar crashing,
  or worse, the compositor crashing
  ([#129](https://codeberg.org/dnkl/yambar/issues/129)).
* kib/kb, mib/mb and gib/gb formatters were inverted.


### Contributors

* [sochotnicky](https://codeberg.org/sochotnicky)
* Alexandre Acebedo
* anb
* Baptiste Daroussin
* Catterwocky
* horus645
* Jan Beich
* mz
* natemaia
* nogerine
* Soc Virnyl S. Estela
* Vincent Fischer


## 1.7.0

### Added

* i3: `persistent` attribute, allowing persistent workspaces
  ([#72](https://codeberg.org/dnkl/yambar/issues/72)).
* bar: `border.{left,right,top,bottom}-width`, allowing the width of
  each side of the border to be configured
  individually. `border.width` is now a short-hand for setting all
  four borders to the same value
  ([#77](https://codeberg.org/dnkl/yambar/issues/77)).
* bar: `layer: top|bottom`, allowing the layer which the bar is
  rendered on to be changed. Wayland only - ignored on X11.
* river: `all-monitors: false|true`.
* `-d,--log-level=info|warning|error|none` command line option
  ([#84](https://codeberg.org/dnkl/yambar/issues/84)).
* river: support for the river-status protocol, version 2 (‘urgent’
  views).
* `online` tag to the `alsa` module.
* alsa: `volume` and `muted` options, allowing you to configure which
  channels to use as source for the volume level and muted state.
* foreign-toplevel: Wayland module that provides information about
  currently opened windows.
* alsa: support for capture devices.
* network: `ssid`, `signal`, `rx-bitrate` and `rx-bitrate` tags.
* network: `poll-interval` option (for the new `signal` and
  `*-bitrate` tags).
* tags: percentage tag formatter, for range tags: `{tag_name:%}`.
* tags: kb/mb/gb, and kib/mib/gib tag formatters.
* clock: add a config option to show UTC time.

### Changed

* bar: do not add `spacing` around empty (zero-width) modules.
* alsa: do not error out if we fail to connect to the ALSA device, or
  if we get disconnected. Instead, keep retrying until we succeed
  ([#86](https://codeberg.org/dnkl/yambar/issues/86)).


### Fixed

* `yambar --backend=wayland` always erroring out with _”yambar was
  compiled without the Wayland backend”_.
* Regression: `{where}` tag not being expanded in progress-bar
  `on-click` handlers.
* `alsa` module causing yambar to use 100% CPU if the ALSA device is
  disconnected ([#61](https://codeberg.org/dnkl/yambar/issues/61)).


### Contributors

* [paemuri](https://codeberg.org/paemuri)
* [ericonr](https://codeberg.org/ericonr)
* [Nulo](https://nulo.in)


## 1.6.2

### Added

* Text shaping support.
* Support for middle and right mouse buttons, mouse wheel and trackpad
  scrolling ([#39](https://codeberg.org/dnkl/yambar/issues/39)).
* script: polling mode. See the new `poll-interval` option
  ([#67](https://codeberg.org/dnkl/yambar/issues/67)).


### Changed

* doc: split up **yambar-modules**(5) into multiple man pages, one for
  each module ([#15](https://codeberg.org/dnkl/yambar/issues/15)).
* fcft >= 2.4.0 is now required.
* sway-xkb: non-keyboard inputs are now ignored
  ([#51](https://codeberg.org/dnkl/yambar/issues/51)).
* battery: don’t terminate (causing last status to “freeze”) when
  failing to update; retry again later
  ([#44](https://codeberg.org/dnkl/yambar/issues/44)).
* battery: differentiate "Not Charging" and "Discharging" in state
  tag of battery module.
  ([#57](https://codeberg.org/dnkl/yambar/issues/57)).
* string: use HORIZONTAL ELLIPSIS instead of three regular periods
  when truncating a string
  ([#73](https://codeberg.org/dnkl/yambar/issues/73)).


### Fixed

* Crash when merging non-dictionary anchors in the YAML configuration
  ([#32](https://codeberg.org/dnkl/yambar/issues/32)).
* Crash in the `ramp` particle when the tag’s value was out-of-bounds
  ([#45](https://codeberg.org/dnkl/yambar/issues/45)).
* Crash when a string particle contained `{}`
  ([#48](https://codeberg.org/dnkl/yambar/issues/48)).
* `script` module rejecting range tag end values containing the digit
  `9` ([#60](https://codeberg.org/dnkl/yambar/issues/60)).


### Contributors

* [novakane](https://codeberg.org/novakane)
* [mz](https://codeberg.org/mz)


## 1.6.1

### Changed

* i3: workspaces with numerical names are sorted separately from
  non-numerically named workspaces
  ([#30](https://codeberg.org/dnkl/yambar/issues/30)).


### Fixed

* mpd: `elapsed` tag not working (regression, introduced in 1.6.0).
* Wrong background color for (semi-) transparent backgrounds.
* battery: stats sometimes getting stuck at 0, or impossibly large
  values ([#25](https://codeberg.org/dnkl/yambar/issues/25)).


## 1.6.0

### Added

* alsa: `percent` tag. This is an integer tag that represents the
  current volume as a percentage value
  ([#10](https://codeberg.org/dnkl/yambar/issues/10)).
* river: added documentation
  ([#9](https://codeberg.org/dnkl/yambar/issues/9)).
* script: new module, adds support for custom user scripts
  ([#11](https://codeberg.org/dnkl/yambar/issues/11)).
* mpd: `volume` tag. This is a range tag that represents MPD's current
  volume in percentage (0-100)
* i3: `sort` configuration option, that controls how the workspace
  list is sorted. Can be set to one of `none`, `ascending` or
  `descending`. Default is `none`
  ([#17](https://codeberg.org/dnkl/yambar/issues/17)).
* i3: `mode` tag: the name of the currently active mode


### Fixed

* YAML parsing error messages being replaced with a generic _“unknown
  error”_.
* Memory leak when a YAML parsing error was encountered.
* clock: update every second when necessary
  ([#12](https://codeberg.org/dnkl/yambar/issues/12)).
* mpd: fix compilation with clang
  ([#16](https://codeberg.org/dnkl/yambar/issues/16)).
* Crash when the alpha component in a color value was 0.
* XCB: Fallback to non-primary monitor when the primary monitor is
  disconnected ([#20](https://codeberg.org/dnkl/yambar/issues/20))


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
