yambar-modules-xkb(5)

# NAME
xkb - This module monitors the currently active XKB keyboard layout

# DESCRIPTION

This module monitors the currently active XKB keyboard layout and
lock-key states.

Note: this module is X11 only. It does not work in Wayland.

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  name
:  string
:  Name of currently selected layout, long version (e.g. "English (US)")
|  symbol
:  string
:  Name of currently selected layout, short version (e.g. "us")
|  caps_lock
:  bool
:  True if *CapsLock* is enabled
|  num_lock
:  bool
:  True if *NumLock* is enabled
|  scroll_lock
:  bool
:  True if *ScrollLock* is enabled

# CONFIGURATION

No additional attributes supported, only the generic ones (see
*GENERIC CONFIGURATION* in *yambar-modules*(5))

# EXAMPLES

```
bar:
  left:
    - xkb:
        content:
          string: {text: "{symbol}"}
```

# SEE ALSO

*yambar-modules-sway-xkb*(5), *yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

