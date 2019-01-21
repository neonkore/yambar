# F00bar

[![pipeline status](https://gitlab.com/dnkl/f00bar/badges/master/pipeline.svg)](https://gitlab.com/dnkl/f00bar/commits/master)


## Index

1. [Introduction](#introduction)
1. [Configuration](#configuration)
    1. [Overview](#overview)
    1. [Types](#types)
    1. [Bar](#bar)
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


## Introduction

![screenshot](screenshot.png "Example configuration")

**f00bar** is a light-weight and configurable status panel (_bar_, for
short) for X.

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


## Bar

| Name          | Type   | Req. | Description
|---------------|--------|------|------------
| height        | int    | yes  | The height of the bar, in pixels
| location      | enum   | yes  | One of `top` or `bottom`
| background    | color  | yes  | Background color
| monitor       | string | no   | Monitor to place the bar. If not specified, the primary monitor will be used.
| left-spacing  | int    | no   | Space, in pixels, added **before** each module
| right-spacing | int    | no   | Space, in pixels, added **after** each module
| spacing       | int    | no   | Short-hand for setting both `left-spacing` and `right-spacing`
| left-margin   | int    | no   | Left-side margin, in pixels
| right-margin  | int    | no   | Right-side margin, in pixels
| margin        | int    | no   | Short-hand for setting both `left-margin` and `right-margin`
| border        | dict   | no   | Configures a border around the status bar
| border.width  | int    | yes  | Width, in pixels, of the border
| border.color  | color  | yes  | The color of the border
| font          | font   | no   | Default font to use in modules and particles
| foreground    | color  | no   | Default foreground (text) color to use
| left          | list   | no   | Left-aligned modules
| center        | list   | no   | Center-aligned modules
| right         | list   | no   | Right-aligned modules

The value of each item in the `left`, `center` and `right` lists is a _module_.


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


### Alsae
### Generic Configuration

**All** modules support the following attributes:

| Name    | Type     | Description
|---------|----------|------------
| content | particle | A particle describing how the module's information is to be rendered
| anchors | dict     | Free-to-use dictionary, where you can put yaml anchor definitions


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


