yambar-modules-dwl(5)

# NAME
dwl - This module provides information about dwl tags, and information.

# DESCRIPTION

This module provides a map of each tags present in dwl.

Each tags has its _id_, its _name_, its status (_selected_, _empty_, _urgent_)
and the global data like _title_, _appid_, _fullscreen_, _floating_,
_selmon_, and _layout_). The tags start a 1. For needs where
you only want information about the global data and not the _tags_,
there is a tag with the id _0_ that contains only the global data.

This module will track *only* the monitor where yambar was launched on.
If you have a multi monitor setup, please launch yambar on each of your
monitors.

Please, be aware that only *one instance* of this module is supported.
Running multiple instances at the same time may result in 
*undefined behavior*.

# TAGS

[[ *Name*
:[ *Type*
:< *Description*
|  id
:  int
:  Dwl tag id.
|  name
:  string
:  The name of the tag (defaults to _id_ if not set).
|  selected
:  bool
:  True if the tag is currently selected.
|  empty
:  bool
:  True if there are no windows in the tag.
|  urgent
:  bool
:  True if the tag has the urgent flag set.
|  title
:  string
:  The currently focused window's title.
|  appid
:  string
:  The currently focused window's application id.
|  fullscreen
:  bool
:  True if there is a fullscreen window in the current tag.
|  floating
:  bool
:  True if there is a floating window in the current tag.
|  selmon
:  bool
:  True if the monitor is actually focused.
|  layout
:  string
:  The actual layout name of the tag.

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:< *Description*
|  number-of-tags
:  int
:  yes
:  The number of defined tags in the dwl `config.def.h`.
|  name-of-tags
:  list
:  false
:  The name of the tags (must have the same length that _number-of-tags_).
|  dwl-info-filename
:  string
:  yes
:  The filepath to the log emitted by dwl when running.

# EXAMPLES

```
bar:
  left:
    - dwl:
        number-of-tags: 9
        dwl-info-filename: "/home/ogromny/dwl_info"
        name-of-tags: [ , , , , , , , ,  ]
        content:
          list:
            items:
              - map:
                  conditions:
                    # default tag
                    id == 0: {string: {text: "{layout} {title}"}}

                    selected: {string: {text: "-> {name}"}}
                    ~empty: {string: {text: "{name}"}}
                    urgent: {string: {text: "=> {name} <="}}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

