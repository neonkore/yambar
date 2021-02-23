yambar-modules-removables(5)

# NAME
removables - This module detects removable drives

# DESCRIPTION

This module detects removable drives (USB sticks, CD-ROMs) and
instantiates the provided _content_ particle for each detected drive.

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  vendor
:  string
:  Name of the drive vendor
|  model
:  string
:  Drive model name
|  optical
:  bool
:  True if the drive is an optical drive (CD-ROM, DVD-ROM etc)
|  device
:  string
:  Volume device name (typically */dev/sd?*)
|  size
:  range
:  The volume's size, in bytes. The tag's maximum value is set to the
   underlying block device's size
|  label
:  string
:  The volume's label, or its size if it has no label
|  mounted
:  bool
:  True if the volume is mounted
|  mount_point
:  string
:  Path where the volume is mounted, or *""* if it is not mounted

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  left-spacing
:  int
:  no
:  Space, in pixels, in the left side of each rendered volume
|  right-spacing
:  int
:  no
:  Space, in pixels, on the right side of each rendered volume
|  spacing
:  int
:  no
:  Short-hand for setting both _left-spacing_ and _right-spacing_
|  ignore
:  list of strings
:  no
:  List of device paths that should be ignored (e.g. /dev/mmcblk0, or /dev/mmcblk0p1)

# EXAMPLES

```
bar:
  right:
    - removables:
        content:
          map:
            tag: mounted
            values:
              false:
                string:
                  on-click: udisksctl mount -b {device}
                  text: "{label}"
              true:
                string:
                  on-click: udisksctl unmount -b {device}
                  text: "{label}"
                  deco: {underline: {size: 2, color: ffffffff}}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

