yambar-particles(5)

# NAME
yambar-particles - configuration file

# DESCRIPTION

Particles are what renders the tags provided by modules. Each particle
defines its own set of configuration attributes. However, the
following attributes are supported by all particles:

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  left-margin
:  int
:  no
:  Space, in pixels, on the left side of the particle
|  right-margin
:  int
:  no
:  Space, in pixels, on the right side of the particle
|  margin
:  int
:  no
:  Short-hand for setting both _left-margin_ and _right-margin_
|  font
:  font
:  no
:  Font to use. Note that this is an inherited attribute; i.e. you can
   set it on e.g. a _list_ particle, and it will apply to all
   particles in the list.
|  foreground
:  color
:  no
:  Foreground (text) color. Just like _font_, this is an inherited attribute.
|  on-click
:  string
:  no
:  Command to execute when the particle is clicked. Tags can be
   used. Note that the string is *not* executed in a shell.
|  deco
:  decoration
:  no
:  Decoration to apply to the particle. See *yambar-decorations*(5)

# STRING

This is the most basic particle. It takes a format string, consisting
of free text mixed with tag specifiers.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  text
:  string
:  yes
:  Format string. Tags are spcified with _{tag_name}_. Some tag types
   have suffixes that can be appended (e.g. _{tag_name:suffix}_). See
   *yambar-modules*(5)).
|  max
:  int
:  no
:  Sets the rendered string's maximum length. If the final string's
   length exceeds this, the rendered string will be truncated, and
   "..."  will be appended. Note that the trailing "..." are
   *included* in the maximum length. I.e. if you set _max_ to '5', you
   will only get *2* characters from the string.

## EXAMPLES

```
content:
  string:
    text: "hello, this is footag's value: {footag}"
```

# EMPTY

This particle is a place-holder. While it does not render any tags,
margins and decortions are rendered.

## CONFIGURATION

None

## EXAMPLES

```
content:
  empty: {}
```

# LIST

This particle is a list (or sequence, if you like) of other
particles. It can be used to render e.g. _string_ particles with
different font and/or color formatting. Or ay other particle
combinations.

But note that this means you *cannot* set any attributes on the _list_
particle itself.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  items
:  list
:  yes
:  List of sub particles
|  left-spacing
:  int
:  no
:  Space, in pixels, *between* the sub particles.
|  right-spacing
:  int
:  no
:  Space, in pixels, *between* the sub particles. Note: default=2
|  spacing
:  int
:  no
:  Short-hand for setting both _left-spacing_ and _right-spacing_

## EXAMPLES

```
content:
  list:
    spacing: 5
    items:
      - string: {text: hello}
      - string: {text: world}
```

Many times, the only attribute you need to set is _items_. In this
case, there is a shorter form. Instead of:

```
content:
  list:
    items:
      - string: ...
      - string: ...
```

you can list the items directly:

```
content:
  - string: ...
  - string: ...
```

# MAP

This particle maps the values of a specific tag to different
particles. In addition to explicit tag values, you can also specify a
default/fallback particle.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  tag
:  string
:  yes
:  The tag (name of) which values should be mapped
|  values
:  associative array
:  yes
:  An associative array of tag values mapped to particles
|  default
:  particle
:  no
:  Default particle to use, when tag's value does not match any of the
   mapped values.

## EXAMPLES

```
content:
  map:
    tag: tag_name
    default:
      string:
        text: this is the default particle; the tag's value is now {tag_name}
    values:
      one_value:
        string:
          text: tag's value is now one_value
      another_value:
        string:
          text: tag's value is now another_value

```

# RAMP

This particle uses a range tag to index into an array of
particles. This can be used for example to map volume to a
volume-level icon, or a battery's capacity level to a battery
indicator.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  tag
:  string
:  yes
:  The range tag (name of) to use as index
|  items
:  list
:  yes
:  List of particles. Note that the tag value is *not* used as-is; its
   minimum and maximum values are used to map the tag's range to the
   particle list's range.

## EXAMPLES

```
content:
  ramp:
    tag: capacity
    items:
      - string: {text: }
      - string: {text: }
      - string: {text: }
      - string: {text: }
      - string: {text: }
```

# PROGRESS-BAR

This particle renders a range tag's value as a progress bar. You
control the looks of it by defining the particles to use for the
progress bar's start and end, it's size, which particles to use for
the range that has been completed, the range that has yet to be
completed, and the particle to use as the progress bar's current value
indicator.

This particle also supports _realtime_ tags, and will then auto-update
itself when needed.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  tag
:  string
:  yes
:  The range or realtime tag (name of) which value will be used as the
   progress bar's value.
|  length
:  int
:  yes
:  The size/length of the progress bar, in characters. Note that the
   _start_, _end_ and _indicator_ particles are *not* included.
|  start
:  particle
:  yes
:  The progress bar's starting character
|  end
:  particle
:  yes
:  The progress bar's ending character
|  fill
:  particle
:  yes
:  Particle to use in the completed range
|  empty
:  particle
:  yes
:  Particle to use in the not-yet-completed range
|  indicator
:  particle
:  yes
:  Particle representing the progress bar's current value

## EXAMPLES

```
content:
  progres-bar:
    tag: tag_name
    length: 20
    start: {string: {text: ├}}
    end: {string: {text: ┤}}
    fill: {string: {text: ─}}
    empty: {string: {text: ╌}}
    indicator: {string: {text: ┼}}
```

# SEE ALSO

*yambar-tags*(5), *yambar-decorations*(5)
