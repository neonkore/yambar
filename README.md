# F00bar

[![pipeline status](https://gitlab.com/dnkl/f00bar/badges/master/pipeline.svg)](https://gitlab.com/dnkl/f00bar/commits/master)


## Index

1. [Introduction](#introduction)
1. [Modules](#modules)
  1. [Alsa](#alsa)
  1. [backlight](#backlight)
  1. [battery](#battery)
  1. [clock](#clock)
  1. [i3](#i3)
  1. [label](#label)
  1. [mpd](#mpd)
  1. [network](#network)
  1. [removables](#removables)
  1. [xkb](#xkb)
  1. [xwindow](#xwindow)
1. [Particles](#particles)
  1. [empty](#empty)
  1. [list](#list)
  1. [map](#map)
  1. [progress  1.bar](#progress_bar)
  1. [ramp](#ramp)
  1. [string](#string)
1. [Decorations](#decorations)
  1. [background](#background)
  1. [stack](#stack)
  1. [underline](#underline)
1. [Configuration](#configuration)
  1. [Overview](#overview)
  1. [Types](#types)
  1. [Bar](#bar)


## Introduction

![screenshot](screenshot.png "Example configuration")

**f00bar** is a light-weight and configurable status panel (_bar_, for
short) for X.

It has a number of _modules_ that provides information in the form of
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


## Modules

- [alsa](#alsa)
- [backlight](#backlight)
- [battery](#battery)
- [clock](#clock)
- [i3](#i3)
- [label](#label)
- [mpd](#mpd)
- [network](#network)
- [removables](#removables)
- [xkb](#xkb)
- [xwindow](#xwindow)


### Alsa
### Backlight
### Battery
### Clock
### I3
### Label
### Mpd
### Network
### Removables
### Xkb
### Xwindow


## Particles

- [empty](#empty)
- [list](#list)
- [map](#map)
- [progress-bar](#progress_bar)
- [ramp](#ramp)
- [string](#string)


### Empty
### List
### Map
### Progress-bar
### Ramp
### String


## Decorations

- [background](#background)
- [stack](#stack)
- [underline](#underline)


### Background
### Stack
### Underline


## Configuration

### Overview

F00bar is configured using YAML, in `~/.config/f00bar/config.yml`. It
must define a top-level dictionary named **bar**:

    bar:
      height: 26
      location: top
      background: 000000ff



### Types

There are a couple types used that are specific to f00bar.

- **font**: this is a string in _fontconfig_ format. Example of valid values:
  + Font Awesome 5 Brands
  + Font Awesome 5 Free:style=solid
  + Dina:pixelsize=10:slant=italic
  + Dina:pixelsize=10:weight=bold
- **color**: an rgba hexstring; RRGGBBAA. Examples:
  + ffffffff: white, no transparancy
  + 000000ff: black, no transparancy
  + 00ff00ff: green, no transparancy
  + ff000099: red, semi-transparent


### Bar

- `height` (_int_, **required**): the height of the bar, in
  pixels. Note that the bar will _always_ occupy the entire width of
  the monitor.
- `location` (_enum_, **required**): one of `top` or `bottom`. Should
  be self-explanatory.
- `background` (_color_, **required**): background color, in
  _rgba_. Thus, in the example above, the background is set to _black_
- `left-spacing` (_int_): space, in pixels, added **before** each module
- `right-spacing` (_int_): space, in pixels, added **after** each module
- `spacing` (_int_): short-hand for setting both `left-spacing` and
  `right-spacing`
- `left-margin` (_int_): left-side margin, in pixels
- `right-margin` (_int_): right-side margin, in pixels
- `margin` (_int_): short-hand for setting both `left-margin` and
  `right-margin`
- `border` (_dictionary_): configures a border around the status bar
- `font` (_font_): default font to use
- `foreground` (_color_): default foreground (text) color to use
- `left` (_list_): left-aligned modules
- `center` (_list_): center-aligned modules
- `right` (_list_): right-aligned modules

The `border` dictionary has the following attributes:

- `width` (_int_, **required**): with, in pixels, of the border
- `color` (_color_, **required**): the color of the border
