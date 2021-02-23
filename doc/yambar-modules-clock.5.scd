yambar-modules-clock(5)

# NAME
clock - This module provides the current date and time

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  time
:  string
:  Current time, formatted using the _time-format_ attribute
|  date
:  string
:  Current date, formatted using the _date-format_ attribute

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  time-format
:  string
:  no
:  *strftime* formatter for the _time_ tag (default=*%H:%M*)
|  date-format
:  string
:  no
:  *strftime* formatter for the _date_ date (default=*%x*)

# EXAMPLES

```
bar:
  left:
    - clock:
        time-format: "%H:%M %Z"
        content:
          string: {text: "{date} {time}"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

