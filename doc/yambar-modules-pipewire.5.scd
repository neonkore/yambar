yambar-modules-pipewire(5)

# NAME
pipewire - Monitors pipewire for volume, mute/unmute, device change

# TAGS

[[ *Name*
:[ *Type*
:< *Description*
|  type
:  string
:  Either "source" (capture) or "sink" (speaker)
|  name
:  string
:  Current device name
|  description
:  string
:  Current device description
|  form_factor
:  string
:  Current device form factor (headset, speaker, mic, etc)
|  bus
:  string
:  Current device bus (bluetooth, alsa, etc)
|  icon
:  string
:  Current device icon name
|  muted
:  bool
:  True if muted, otherwise false
|  linear_volume
:  range
:  Linear volume in percentage (with 0 as min and 100 as max)
|  cubic_volume
:  range
:  Cubic volume (used by pulseaudio) in percentage (with 0 as min and 100 as max)


# CONFIGURATION

No additional attributes supported, only the generic ones (see
*GENERIC CONFIGURATION* in *yambar-modules*(5))


# EXAMPLES

```
bar:
  left:
    - pipewire:
        anchors:
          volume: &volume
            conditions:
              muted: {string: {text: "{linear_volume}%", foreground: ff0000ff}}
              ~muted: {string: {text: "{linear_volume}%"}}
        content:
          list:
            items:
              - map:
                  conditions:
                    type == "sink":
                      map:
                        conditions:
                          icon == "audio-headset-bluetooth":
                            string: {text: "🎧  "}
                        default:
                          - ramp:
                              tag: linear_volume
                              items:
                                - string: {text: "🔈  "}
                                - string: {text: "🔉  "}
                                - string: {text: "🔊  "}
                    type == "source":
                      - string: {text: "🎙  "}
              - map:
                  <<: *volume
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

