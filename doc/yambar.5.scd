yambar(5)

# NAME
yambar - configuration file

# DESCRIPTION

A yambar configuration file is a yaml formatted document containing an
associative array named _bar_. You may define other top-level entities
and reference them using anchors.

Besides the normal yaml types, there are a couple of yambar specific
types that are frequently used:

- *font*: this is a string in _fontconfig_ format. Example of valid values:
	- Font Awesome 5 Brands
	- Font Awesome 5 Free:style=solid
	- Dina:pixelsize=10:slant=italic
	- Dina:pixelsize=10:weight=bold
- *color*: an rgba hexstring; _RRGGBBAA_. Examples:
	- ffffffff: white, no transparency
	- 000000ff: black, no transparency
	- 00ff00ff: green, no transparency
	- ff000099: red, semi-transparent

# FORMAT
[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  height
:  int
:  yes
:  The height of the bar, in pixels (*not* including border)
|  location
:  enum
:  yes
:  one of _top_ or _bottom_
|  background
:  color
:  yes
:  Background color
|  monitor
:  string
:  no
:  Monitor to place the bar on. If not specified, the primary monitor will be
   used
|  layer
:  string
:  no
:  Layer to put bar on. One of _top_ or _bottom_. Wayland only
|  left-spacing
:  int
:  no
:  Space, in pixels, added *before* each module
|  right-spacing
:  int
:  no
:  Space, in pixels, added *after* each module
|  spacing
:  int
:  no
:  Short-hand for setting both _left-spacing_ and _right-spacing_
|  left-margin
:  int
:  no
:  Left-side margin, in pixels
|  right-margin
:  int
:  no
:  Right-side margin, in pixels
|  margin
:  int
:  no
:  Short-hand for setting both _left-margin_ and _right-margin_
|  border
:  associative array
:  no
:  Configures the border around the status bar
|  border.left-width
:  int
:  no
:  Width of the border on the left side, in pixels
|  border.right-width
:  int
:  no
:  Width of the border on the right side, in pixels
|  border.top-width
:  int
:  no
:  Width of the border on the top side, in pixels
|  border.bottom-width
:  int
:  no
:  Width of the border on the bottom side, in pixels
|  border.width
:  int
:  no
:  Short-hand for setting _border.left/right/top/bottom-width_
|  border.color
:  color
:  no
:  The color of the border
|  border.left-margin
:  int
:  no
:  Left-side margin, in pixels, from screen edge to bar
|  border.right-margin
:  int
:  no
:  Right-side margin, in pixels, from screen edge to bar
|  border.top-margin
:  int
:  no
:  Top margin, in pixels, from screen edge to bar
|  border.bottom-margin
:  int
:  no
:  Bottom margin, in pixels, from screen edge to bar
|  border.margin
:  int
:  no
:  Short-hand for setting _border.left/right/top/bottom-margin_
|  font
:  font
:  no
:  Default font to use in modules and particles
|  foreground
:  color
:  no
:  Default foreground (text) color to use
|  trackpad-sensitivity
:  int
:  no
:  How easy it is to trigger wheel-up and wheel-down on-click
   handlers. Higher values means you need to drag your finger a longer
   distance. The default is 30.
|  left
:  list
:  no
:  Left-aligned modules (see *yambar-modules*(5))
|  center
:  list
:  no
:  Center-aligned modules (see *yambar-modules*(5))
|  right
:  list
:  no
:  Right-aligned modules (see *yambar-modules*(5))

# EXAMPLES

Top-oriented bar with a single, right-aligned, module: the *clock*,
rendered as a simple string displaying only the time (not date).

```
bar:
  location: top
  height: 26
  background: 00000066

  right:
    - clock:
        content:
          - string: {text: "{time}"}
```

# FILES
The configuration file is searched for (in this order):

- _$XDG_CONFIG_HOME/yambar/config.yml_
- _$HOME/.config/yambar/config.yml_

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5),
*yambar-decorations*(5)
