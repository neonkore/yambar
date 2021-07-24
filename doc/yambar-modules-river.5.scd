yambar-modules-river(5)

# NAME
river - This module provide information about the river tags

# DESCRIPTION

This module uses river's (https://github.com/ifreund/river, a dynamic
tiling Wayland compositor) status protocol to provide information
about the river tags.

It has an interface similar to the i3/sway module.

The configuration for the river module specifies one _title_ particle,
which will be instantiated with tags representing the currently active
seat and the currently focused view's title.

It also specifies a _content_ template particle, which is instantiated
once for all 32 river tags. This means you probably want to use a
*map* particle to hide unused river tags.

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  id
:  int
:  River tag number
|  visible
:  bool
:  True if the river tag is focused by at least one output (i.e. visible on at least one monitor).
|  focused
:  bool
:  True if the river tag is _visible_ and has keyboard focus.
|  occupied
:  bool
:  True if the river tag has views (i.e. windows).
|  state
:  string
:  Set to *focused* if _focused_ is true, *unfocused* if _visible_ is true, but _focused_ is false, or *invisible* if the river tag is not visible on any monitors.
|  seat
:  string
:  The name of the currently active seat (*title* particle only, see CONFIGURATION)
|  title
:  string
:  The focused view's title (*title* particle only, see CONFIGURATION)

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  title
:  particle
:  no
:  Particle that will be instantiated with the _seat_ and _title_ tags.
|  content
:  particle
:  yes
:  Template particle that will be instantiated once for all of the 32 river tags.

# EXAMPLES

```
bar:
  left:
    - river:
        title: {string: { text: "{seat} - {title}" }}
        content:
          map:
            tag: occupied
            values:
              false: {empty: {}}
              true:
                string:
                  margin: 5
                  text: "{id}: {state}"
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

