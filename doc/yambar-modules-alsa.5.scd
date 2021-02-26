yambar-modules-alsa(5)

# NAME
alsa - Monitors an alsa soundcard for volume and mute/unmute changes

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  volume
:  range
:  Volume level, with min and max as start and end range values
|  percent
:  range
:  Volume level, as a percentage
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
:  The soundcard name. *default* might work.
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

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

