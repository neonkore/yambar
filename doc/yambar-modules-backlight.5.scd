yambar-modules-backlight(5)

# NAME
backlight - This module reads monitor backlight status

# DESCRIPTION
This module reads monitor backlight status from
_/sys/class/backlight_, and uses *udev* to monitor for changes.

# TAGS

[[ *Name*
:[ *Type*
:< *Description*
|  brightness
:  range
:  The current brightness level, in absolute value
|  percent
:  range
:  The current brightness level, in percent

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  name
:  string
:  yes
:  The backlight device's name (one of the names in */sys/class/backlight*)

# EXAMPLES

```
bar:
  left:
    - backlight:
        name: intel_backlight
        content:
          string: {text: "backlight: {percent}%"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

