yambar-modules-foreign-toplevel(5)

# NAME
foreign-toplevel - This module provides information about toplevel windows in Wayland

# DESCRIPTION

This module uses the _wlr foreign toplevel management_ Wayland
protocol to provide information about currently open windows, such as
their application ID, window title, and their current state
(maximized/minimized/fullscreen/activated).

The configuration for the foreign-toplevel module specifies a
_template_ particle. This particle will be instantiated once for each
window.

Note: Wayland only.


# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  app-id
:  string
:  The application ID (typically the application name)
|  title
:  string
:  The window title
|  maximized
:  bool
:  True if the window is currently maximized
|  minimized
:  bool
:  True if the window is currently minimized
|  fullscreen
:  bool
:  True if the window is currently fullscreened
|  activated
:  bool
:  True if the window is currently activated (i.e. has focus)


# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  content
:  particle
:  yes
:  Template particle that will be instantiated once for each window
|  all-monitors
:  bool
:  no
:  When set to true, only windows on the same monitor the bar will be
   used. The default is false.


# EXAMPLES

```
bar:
  left:
    - foreign-toplevel:
        content:
          map:
            tag: activated
            values:
              false: {empty: {}}
              true:
                - string: {text: "{app-id}: {title}"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)
