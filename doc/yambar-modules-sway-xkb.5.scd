yambar-modules-sway-xkb(5)

# NAME
sway-xkb - This module monitor input devices' active XKB layout

# DESCRIPTION

This module uses *Sway* extensions to the I3 IPC API to monitor input
devices' active XKB layout. As such, it requires Sway to be running.

*Note* that the _content_ configuration option is a *template*;
*sway-xkb* will instantiate a particle list, where each item is
instantiated from this template, and represents an input device.

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  id
:  string
:  Input device identifier
|  layout
:  string
:  The input device's currently active XKB layout

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  identifiers
:  list of strings
:  yes
:  Identifiers of input devices to monitor. Use _swaymsg -t get_inputs_ to see available devices.
|  content
:  particle
:  yes
:  A particle template; each existing input device will be instantiated with this template.
|  left-spacing
:  int
:  no
:  Space, in pixels, in the left side of each rendered input device
|  right-spacing
:  int
:  no
:  Space, in pixels, on the right side of each rendered input device
|  spacing
:  int
:  no
:  Short-hand for setting both _left-spacing_ and _right-spacing_

# EXAMPLES

```
bar:
  left:
    - sway-xkb:
        identifiers:
          - 1523:7:HID_05f3:0007
          - 7247:2:USB_USB_Keykoard
        spacing: 5
        content: {string: {text: "{id}: {layout}"}}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)
