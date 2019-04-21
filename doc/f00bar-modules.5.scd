f00bar-modules(5)

# NAME
f00bar-modules - configuration file

# DESCRIPTION

Modules are what monitors your system and provides data for the status
bar.

All modules expose their data through *tags*. Each tag has a *name*,
*type* and *value*. The name and type is fixed, while the value
typically changes over time. See *f00bar-tags(5)*.

The tags are rendered by _particles_. Each particle has its own way of
representing tag values. The simplest one is the _string_ particle,
which renders a text representation of the tag value. See
*f00bar-particles*(5).

# FORMAT

Each module defines its own configuration format. However, the
following attributes are supported by all modules:

[[ *Name*
:[ *Type*
:[ *Description*
|  content
:  particle
:  A particle describing how the module's information is to be rendered.
   See *f00bar-particles*(5)
|  anchors
:  associative array
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

# ALSA

Monitors an alsa soundcard for volume and mute/unmute changes.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  volume
:  range
:  Volume level, with min and max as start and end range values
|  muted
:  bool
:  True if muted, otherwise false


## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  card
:  string
:  yes
:  The soundcard name. _Default_ might work.
|  mixer
:  string
:  yes
:  Mixer channel to monitor. _Master_ might work.

## EXAMPLES

```
bar:
  left:
    - alsa:
        card: hw:PCH
        mixer: Master
        content: {string: {text: "{volume}"}}
```

# SEE ALSO

*f00bar-particles*(5), *f00bar-tags*(5), *f00bar-decorations*(5)
