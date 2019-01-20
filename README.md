# F00bar

[![pipeline status](https://gitlab.com/dnkl/f00bar/badges/master/pipeline.svg)](https://gitlab.com/dnkl/f00bar/commits/master)

![screenshot](screenshot.png "Example configuration")


## Index

1. [Configuration](#configuration)


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
