# Yambar

## Index

1. [Introduction](#introduction)
1. [Configuration](#configuration)
1. [Modules](#modules)


## Introduction

![screenshot](screenshot.png "Example configuration")

**yambar** is a light-weight and configurable status panel (_bar_, for
short) for X and Wayland.

It has a number of _modules_ that provide information in the form of
_tags_. For example, the _clock_ module has a _date_ tag that contains
the current date.

The modules do not know _how_ to present the information though. This
is instead done by _particles_. And the user, you, decides _which_
particles (and thus _how_ to present the data) to use.

Furthermore, each particle can have a _decoration_. These are things
like a different background, or an graphical underline.

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

    bar:
      height: 26
      location: top
      background: 000000ff

      right:
        - clock:
            content:
              - string: {text: , font: *awesome}
              - string: {text: "{date}", right-margin: 5}
              - string: {text: , font: *awesome}
              - string: {text: "{time}"}


For details, see the man pages (**yambar**(5) is a good start).


## Modules

Available modules:

* alsa
* backlight
* battery
* clock
* i3 (and Sway)
* label
* mpd
* network
* removables
* xkb (_XCB backend only_)
* xwindow (_XCB backend only_)
