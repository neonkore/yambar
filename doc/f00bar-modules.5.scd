f00bar-modules(5)

# NAME
f00bar-modules - configuration file

# DESCRIPTION

Modules are what monitors your system and provides data for the status
bar.

All modules expose their data through *tags*. Each tag has a *name*,
*type* and *value*. The name and type is fixed, while the value
typically changes over time. See *f00bar-tags(5)*.

The tags are rendered by _particles_. Each particle has its own way of
representing tag values. The simplest one is the _string_ particle,
which renders a text representation of the tag value. See
*f00bar-particles*(5).

# FORMAT

Each module defines its own configuration format. However, the
following attributes are supported by all modules:

[[ *Name*
:[ *Type*
:[ *Description*
|  content
:  particle
:  A particle describing how the module's information is to be rendered.
   See *f00bar-particles*(5)
|  anchors
:  associative array
:  Free-to-use associative array, where you can put yaml anchor definitions
|  font
:  font
:  no
:  Font to use in the content particle. This is an inherited attribute.
|  foreground
:  color
:  no
:  Foreground (text) color of the content particle. This is an
   inherited attribute.

# ALSA

Monitors an alsa soundcard for volume and mute/unmute changes.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  volume
:  range
:  Volume level, with min and max as start and end range values
|  muted
:  bool
:  True if muted, otherwise false


## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  card
:  string
:  yes
:  The soundcard name. _Default_ might work.
|  mixer
:  string
:  yes
:  Mixer channel to monitor. _Master_ might work.

## EXAMPLES

```
bar:
  left:
    - alsa:
        card: hw:PCH
        mixer: Master
        content: {string: {text: "{volume}"}}
```

# BACKLIGHT

This module reads monitor backlight status from
_/sys/class/backlight_, and uses *udev* to monitor for changes.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  brightness
:  range
:  The current brightness level, in absolute value
|  percent
:  range
:  The current brightness level, in percent

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  name
:  string
:  yes
:  The backlight device's name (one of the names in _/sys/class/backlight_)

## EXAMPLES

```
bar:
  left:
    - backlight:
        name: intel_backlight
        content:
          string: {text: "backlight: {percent}%"}
```

# BATTERY

This module reads battery status from _/sys/class/power_supply_ and
uses *udev* to monitor for changes.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  name
:  string
:  Battery device name
|  manufacturer
:  string
:  Name of the battery manufacturer
|  model
:  string
:  Battery model name
|  state
:  string
:  One of *full*, *charging*, *discharging* or *unknown*
|  capacity
:  range
:  capacity left, in percent
|  estimate
:  string
:  Estimated time left (to empty while discharging, or to full while
   charging), formatted as HH:MM.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  name
:  string
:  yes
:  Battery device name (one of the names in _/sys/class/power_supply_)
|  poll-interval
:  int
:  no
:  How often, in seconds, to poll for capacity changes (default=*60*)

## EXAMPLES

```
bar:
  left:
    - battery:
        name: BAT0
        poll-interval: 30
        content:
          string: {text: "BAT: {capacity}% {estimate}"}
```

# CLOCK

This module provides the current date and time.

## TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  time
:  string
:  Current time, formatted using the _time-format_ attribute
|  date
:  string
:  Current date, formatted using the _date-format_ attribute

## CONFIGURATION

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

## EXAMPLES

```
bar:
  left:
    - clock:
        time-format: "%H:%M %Z"
        content:
          string: {text: "{date} {time}"}
```

# SEE ALSO

*f00bar-particles*(5), *f00bar-tags*(5), *f00bar-decorations*(5)
