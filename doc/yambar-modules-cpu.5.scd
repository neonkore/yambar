yambar-modules-cpu(5)

# NAME
cpu - This module provides the CPU usage

# DESCRIPTION

This module reports CPU usage, in percent. The _content_ particle is a
template that is instantiated once for each core, and once for the
total CPU usage.

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  id
:  int
:  Core ID. 0..n represents individual cores, and -1 represents the
   total usage
|  cpu
:  range
:  Current usage of CPU core {id}, in percent

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  interval
:  int
:  no
:  Refresh interval of the CPU usage stats in ms (default=500). Cannot
   be less then 500 ms

# EXAMPLES

## Display total CPU usage as a number
```
bar:
  left:
    - cpu:
        interval: 2500
        content:
          map:
            conditions:
              id < 0:
                - string: {text: , font: Font Awesome 6 Free:style=solid}
                - string: {text: "{cpu}%"}
```

## Display a vertical bar for each core
```
bar:
  left:
    - cpu:
        interval: 2500
        content:
          map:
            conditions:
              id >= 0:
                - ramp:
                    tag: cpu
                    items:
                      - string: {text: ▁}
                      - string: {text: ▂}
                      - string: {text: ▃}
                      - string: {text: ▄}
                      - string: {text: ▅}
                      - string: {text: ▆}
                      - string: {text: ▇}
                      - string: {text: █}
```


# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)
