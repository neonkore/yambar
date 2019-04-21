f00bar-module-alsa(5)

# NAME
f00bar-module-alsa - configuration file

# DESCRIPTION

Monitors an alsa soundcard for volume and mute/unmute changes.

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  volume
:  range
:  Volume level, with min and max as start and end range values
|  muted
:  bool
:  True if muted, otherwise false


# CONFIGURATION

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

# EXAMPLES

```
bar:
  left:
    - alsa:
        card: hw:PCH
        mixer: Master
        content: {string: {text: "{volume}"}}
```
