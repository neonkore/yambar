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
|  font-shaping
:  enum
:  no
:  font-shaping; one of _full_ or _none_. When set to _full_ (the
   default), strings will be "shaped" using HarfBuzz. Requires support
   in fcft.
|  foreground
:  color
:  no
:  Foreground (text) color. Just like _font_, this is an inherited attribute.
|  on-click
:  associative array/string
:  no
:  When set to a string, executes the string as a command when the particle 
   is left-clicked. Tags can be used. Note that the string is *not* 
   executed in a shell. The same applies to all attributes associated with
   it, below.
|  on-click.left
:  string
:  no
:  Command to execute when the particle is left-clicked.
|  on-click.right
:  string
:  no
:  Command to execute when the particle is right-clicked.
|  on-click.middle
:  string
:  no
:  Command to execute when the particle is middle-clicked.
|  on-click.wheel-up
:  string
:  no
:  Command to execute every time a 'wheel-up' event is triggered.
|  on-click.wheel-down
:  string
:  no
:  Command to execute every time a 'wheel-down' event is triggered.
|  on-click.previous
:  string
:  no
:  Command to execute when the particle is clicked with the 'previous' button.
|  on-click.next
:  string
:  no
:  Command to execute when the particle is clicked with the 'next' button.
|  deco
:  decoration
:  no
:  Decoration to apply to the particle. See *yambar-decorations*(5)

## EXAMPLES:

*on-click* as a string (handles left click):
```
content:
  <particle>:
    on-click: command args
```

*on-click* as an associative array (handles other buttons):
```
content:
  <particle>:
    on-click:
      left: command-1
      wheel-up: command-3
      wheel-down: command-4
```

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
:  Format string. Tags are specified with _{tag_name}_. Some tag types
   have suffixes that can be appended (e.g. _{tag_name:suffix}_). See
   *yambar-modules*(5)).
|  max
:  int
:  no
:  Sets the rendered string's maximum length. If the final string's
   length exceeds this, the rendered string will be truncated, and
   "…"  will be appended. Note that the trailing "…" is
   *included* in the maximum length. I.e. if you set _max_ to '5', you
   will only get *4* characters from the string.

## EXAMPLES

```
content:
  string:
    text: "hello, this is footag's value: {footag}"
```

# EMPTY

This particle is a place-holder. While it does not render any tags,
margins and decorations are rendered.

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
particles based on conditions. A condition takes either the form of:

```
<tag> <operation> <value>
```

Or, for boolean tags:

```
<tag>
```

Where <tag> is the tag you would like to map, <operation> is one of:

[-  ==
:- !=
:- >=
:- >
:- <=
:- <

and <value> is the value you would like to compare it to. *If the
value contains any non-alphanumerical characters, you must
surround it with ' " ' *:

```
"hello world"
"@#$%"
```

Negation is done with a preceding '~':

```
~<tag>
~<condition>
```

To match for empty strings, use ' "" ':

```
<tag> == ""
```

Furthermore, you may use the boolean operators:

[-  &&
:- ||

in order to create more complex conditions:

```
<condition1> && <condition2>
```

You may surround <condition> with parenthesis for clarity or
specifying precedence:

```
(<condition>)
<condition1> && (<condition2> || <condition3>)
```

In addition to explicit tag values, you can also specify a
default/fallback particle.

Note that conditions are evaluated in the order they appear. *If
multiple conditions are true, the first one will be used*. This means
that in a configuration such as:

```
tx-bitrate > 1000:
tx-bitrate > 1000000:
```

the second condition would never run, since whenever the second
condition is true, the first is also true. The correct way of doing
this would be to invert the order of the conditions:

```
tx-bitrate > 1000000:
tx-bitrate > 1000:
```


## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  conditions
:  associative array
:  yes
:  An associative array  of conditions (see above) mapped to particles
|  default
:  particle
:  no
:  Default particle to use, none of the conditions are true

## EXAMPLES

```
content:
  map:
    default:
      string:
        text: this is the default particle; the tag's value is now {tag_name}
    conditions:
      tag == one_value:
        string:
          text: tag's value is now one_value
      tag == another_value:
        string:
          text: tag's value is now another_value

```

For a boolean tag:

```
content:
  map:
    conditions:
      tag:
        string:
        text: tag is true
      ~tag:
        string:
        text: tag is false
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
|  min
:  int
:  no
:  If present this will be used as a lower bound instead of the tags
   minimum value.  Tag values falling outside the defined range will
   get clamped to min/max.
|  max
:  int
:  no
:  If present this will be used as an upper bound instead of the tags
   maximum value.  Tag values falling outside the defined range will
   get clamped to min/max.

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
