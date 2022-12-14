yambar-modules-alsa(5)

# NAME
alsa - Monitors an alsa soundcard for volume and mute/unmute changes

# TAGS

[[ *Name*
:[ *Type*
:< *Description*
|  online
:  bool
:  True when the ALSA device has successfully been opened
|  dB
:  range
:  Volume level (in dB), with min and max as start and end range
   values.
|  volume
:  range
:  Volume level (raw), with min and max as start and end range values
|  percent
:  range
:  Volume level, as a percentage. This value is based on the *dB* tag
   if available, otherwise the *volume* tag.
|  muted
:  bool
:  True if muted, otherwise false


# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:< *Description*
|  card
:  string
:  yes
:  The soundcard name. *default* might work.
|  mixer
:  string
:  yes
:  Mixer channel to monitor. _Master_ might work.
|  volume
:  string
:  no
:  The name of the channel to use as source for the volume level
   (default: first available channel, usually "Front Left").
|  muted
:  string
:  no
:  The name of the channel to use as source for the muted state
   (default: first available channel, usually "Front Left").


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

