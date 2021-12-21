yambar-modules-cpu(5)

# NAME
cpu - This module provides the CPU usage

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  cpu
:  range
:  Current usage of the whole CPU in percent
|  cpu<0..X>
:  range
:  Current usage of CPU core X in percent

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  interval
:  int
:  no
:  Refresh interval of the CPU usage stats in ms (default=500). Cannot be less then 500 ms

# EXAMPLES

```
bar:
  left:
    - cpu:
        interval: 2500
        content:
          string: {text: "{cpu1}%"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)
