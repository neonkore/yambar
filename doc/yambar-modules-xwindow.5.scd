yambar-modules-xwindow(5)

# NAME
xwindow - This module provides the application name and window title

# DESCRIPTION

This module provides the application name and window title of the
currently focused window.

Note: this module is X11 only. It does not work in Wayland. If you are
running Sway, take a look at the *i3* module and its _application_ and
_title_ tags.

# TAGS

[[ *Name*
:[ *Type*
:< *Description*
|  application
:  string
:  Name of the application that owns the currently focused window
|  title
:  string
:  The title of the currently focused window

# CONFIGURATION

No additional attributes supported, only the generic ones (see
*GENERIC CONFIGURATION* in *yambar-modules*(5))

# EXAMPLES

```
bar:
  left:
    - xwindow:
        content:
          string: {text: "{application}: {title}"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

