[![CI status](https://ci.codeberg.org/api/badges/dnkl/yambar/status.svg)](https://ci.codeberg.org/dnkl/yambar)

# Yambar

[![Packaging status](https://repology.org/badge/vertical-allrepos/yambar.svg)](https://repology.org/project/yambar/versions)


## Index

1. [Introduction](#introduction)
1. [Configuration](#configuration)
1. [Modules](#modules)
1. [Installation](#installation)
1. [Bugs](#bugs)


## Introduction

![screenshot](screenshot.png "Example configuration")

**yambar** is a lightweight and configurable status panel (_bar_, for
short) for X11 and Wayland, that goes to great lengths to be both CPU
and battery efficient - polling is only done when **absolutely**
necessary.

It has a number of _modules_ that provide information in the form of
_tags_. For example, the _clock_ module has a _date_ tag that contains
the current date.

The modules do not know _how_ to present the information though. This
is instead done by _particles_. And the user, you, decides _which_
particles (and thus _how_ to present the data) to use.

Furthermore, each particle can have a _decoration_ - a background
color or a graphical underline, for example.

There is **no** support for images or icons. use an icon font
(e.g. _Font Awesome_, or _Material Icons_) if you want a graphical
representation.

There are a number of modules and particles builtin. More can be added
as plugins. You can even write your own!

To summarize: a _bar_ displays information provided by _modules_,
using _particles_ and _decorations_. **How** is configured by you.


## Configuration

Yambar is configured using YAML, in `~/.config/yambar/config.yml`. It
must define a top-level dictionary named **bar**:

```yaml
bar:
  height: 26
  location: top
  background: 000000ff

  right:
    - clock:
        content:
          - string: {text: , font: "Font Awesome 6 Free:style=solid:size=12"}
          - string: {text: "{date}", right-margin: 5}
          - string: {text: , font: "Font Awesome 6 Free:style=solid:size=12"}
          - string: {text: "{time}"}
```

For details, see the man pages (**yambar**(5) is a good start).

Example configurations can be found in [examples](examples/configurations).


## Modules

Available modules:

* alsa
* backlight
* battery
* clock
* cpu
* disk-io
* dwl
* foreign-toplevel
* i3 (and Sway)
* label
* mem
* mpd
* network
* pulse
* removables
* river
* script (see script [examples](examples/scripts))
* sway-xkb
* xkb (_XCB backend only_)
* xwindow (_XCB backend only_)


## Installation

To build, first, create a build directory, and switch to it:
```sh
mkdir -p bld/release && cd bld/release
```

Second, configure the build (if you intend to install it globally, you
might also want `--prefix=/usr`):
```sh
meson --buildtype=release ../..
```

Optionally, explicitly disable a backend (or enable, if you want a
configuration error if not all dependencies are met) by adding either
`-Dbackend-x11=disabled|enabled` or
`-Dbackend-wayland=disabled|enabled` to the meson command line.

Three, build it:
```sh
ninja
```

Optionally, install it:
```sh
ninja install
```

## Bugs

Please report bugs to https://codeberg.org/dnkl/yambar/issues

The report should contain the following:

* Which Wayland compositor (and version) you are running
* Yambar version (`yambar --version`)
* Log output from yambar (start yambar from a terminal)
* If reporting a crash, please try to provide a `bt full` backtrace
  **with symbols** (i.e. use a debug build)
* Steps to reproduce. The more details the better
