yambar-modules-i3(5)

# NAME
i3 - This module monitors i3 and sway workspaces

# DESCRIPTION

Unlike other modules where the _content_ attribute is just a single
*particle*, the i3 module's _content_ is an associative array mapping
i3/sway workspace names to a particle.

You can add an empty workspace name, *""*, as a catch-all workspace
particle. The *i3* module will fallback to this entry if it cannot
find the workspace name in the _content_ map.

It also recognizes the special name *current*, which always represents
the currently focused workspace. On Sway, this can be used together
with the _application_ and _title_ tags to replace the X11-only
*xwindow* module.

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  name
:  string
:  The workspace name
|  visible
:  bool
:  True if the workspace is currently visible (on any output)
|  focused
:  bool
:  True if the workspace is currently focused
|  urgent
:  bool
:  True if the workspace has the urgent flag set
|  empty
:  bool
:  True if the workspace is empty
|  state
:  string
:  One of *urgent*, *focused*, *unfocused* or *invisible* (note:
   *unfocused* is when it is visible, but neither focused nor urgent).
|  application
:  string
:  Name of application currently focused on this workspace (Sway only - use the *xwindow* module in i3)
|  title
:  string
:  This workspace's focused window's title
|  mode
:  string
:  The name of the current mode

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  content
:  associative array
:  yes
:  Unlike other modules, _content_ is an associative array mapping
   workspace names to particles. Use *""* to specify a default
   fallback particle, or *current* for the currently active workspace.
|  sort
:  enum
:  no
:  How to sort the list of workspaces; one of _none_, _ascending_ or _descending_, defaults to _none_.
|  persistent
:  list of strings
:  no
:  Persistent workspaces. I.e. workspaces that are never removed, even if empty.
|  left-spacing
:  int
:  no
:  Space, in pixels, on the left-side of each rendered workspace particle
|  right-spacing
:  int
:  no
:  Space, in pixels, on the right-side of each rendered workspace particle
|  spacing
:  int
:  no
:  Short-hand for setting both _left-spacing_ and _right-spacing_

# EXAMPLES

This renders all workspace names, with an *\** indicating the
currently focused one. It also renders the currently focused
application name and window title.

```
bar:
  left:
    - i3:
        content:
          "":
            map:
              tag: state
              default: {string: {text: "{name}"}}
              values:
                focused: {string: {text: "{name}*"}}
          current: { string: {text: "{application}: {title}"}}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

