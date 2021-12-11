yambar-modules-mem(5)

# NAME
mem - This module provides the memory usage

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  free
:  int
:  Free memory in bytes
|  used
:  int
:  Used memory in bytes
|  total
:  int
:  Total memory in bytes
|  percent_free
:  range
:  Free memory in percent
|  percent_used
:  range
:  Used memory in percent

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  interval
:  string
:  no
:  Refresh interval of the memory usage stats in ms (default=500). Cannot be less then 500 ms

# EXAMPLES

```
bar:
  left:
    - mem:
        interval: 2500
        content:
          string: {text: "{used:mb}MB"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)
