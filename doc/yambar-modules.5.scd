yambar-modules(5)

# NAME
yambar-modules - configuration file

# DESCRIPTION

Modules are what monitors your system and provides data for the status
bar.

All modules expose their data through *tags*. Each tag has a *name*,
*type* and *value*. The name and type is fixed, while the value
typically changes over time. See *yambar-tags*(5).

The tags are rendered by _particles_. Each particle has its own way of
representing tag values. The simplest one is the _string_ particle,
which renders a text representation of the tag value. See
*yambar-particles*(5).

Note that all the examples showed below have been kept simple. Here
are a couple of tips that will improve the looks:

## Use list particles to change font and/or color

The _string_ particle, for example, cannot change font or colors in
the middle of its string. To do this, you need to wrap multiple
_string_ particles in a _list_ particle.

This can be useful if you want to use an icon font for a glyph since
the default fallback fonts provided by fontconfig may not favor your
icon font.

Also remember there is a short version for lists (see
*yambar-particles*(5))

For example, to render _backlight_ as " 20%", you could use:

```
content:
  - string:
      font: Font Awesome 5 Free:style=solid:pixelsize=14
      text: 
  - string:
      font: Adobe Helvetica:pixelsize=12
      text: "{percent}%"
```

## Use map particles to handle 'state'

Several modules have a _state_ tag that can be used to render
different particles depending on the module's state.

For example, you might want different things to be shown for a
_network_ interface that is *down* or *up*. You could further
differentiate between an *up* interface that has or has not an IP
address assigned to it.

Below is an example, where a wired connection is not renderer at all
when disconnected.

When connected, it is rendered in the default text color if it is up
and also has an IPv4 address. If it is up, but does not have an IPv4
address, it is rendered in a semi-transparent white color.

Finally, if it is down, or in any other unknown state, it is rendered
in red.

```
content:
  map:
    tag: carrier
    values:
      false: {empty: {}}
      true:
        map:
          tag: state
          default: {string: {text: , font: *awesome, foreground: ffffff66}}
          values:
            up:
              map:
                tag: ipv4
                default: {string: {text: , font: *awesome}}
                values:
                  "": {string: {text: , font: *awesome, foreground: ffffff66}}
```

## Use yaml anchors

You often end up using the same definitions in a lot of places. This
is particular true for fonts. But it can also be true when mapping
state.

In these cases, you can define an anchor point, either at top-level,
or in a module's _anchors_ attribute:

```
awesome: &awesome Font Awesome 5 Free:style=solid:pixelsize=14

```

Then reference it in your particle definitions:

```
content:
  string: {text: , font: *awesome}
```

# GENERIC CONFIGURATION

Each module defines its own configuration format. However, the
following attributes are supported by all modules:

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  content
:  particle
:  yes
:  A particle describing how the module's information is to be rendered.
   See *yambar-particles*(5)
|  anchors
:  associative array
:  no
:  Free-to-use associative array, where you can put yaml anchor definitions
|  font
:  font
:  no
:  Font to use in the content particle. This is an inherited attribute.
|  foreground
:  color
:  no
:  Foreground (text) color of the content particle. This is an
   inherited attribute.

# BUILT-IN MODULES

Available modules have their own pages:

*yambar-modules-alsa*(5)

*yambar-modules-backlight*(5)

*yambar-modules-battery*(5)

*yambar-modules-clock*(5)

*yambar-modules-i3*(5)

*yambar-modules-label*(5)

*yambar-modules-mpd*(5)

*yambar-modules-network*(5)

*yambar-modules-removables*(5)

*yambar-modules-river*(5)

*yambar-modules-script*(5)

*yambar-modules-sway-xkb*(5)

*yambar-modules-sway*(5)

*yambar-modules-xkb*(5)

*yambar-modules-xwindow*(5)

# SEE ALSO

*yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)
