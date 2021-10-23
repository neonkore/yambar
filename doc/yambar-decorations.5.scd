yambar-decorations(5)

# NAME
yambar-decorations - configuration file

# DESCRIPTION

Decorations are optional additions to particles, enabling you to
e.g. change the background color of the entire particle, draw an
underline and so on.

Decorations are applied to the particle's margins as well.

All decorations define their own configuration attributes, and there
are no common attributes.

# BACKGROUND

This decoration sets the particles background color.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  color
:  color
:  yes
:  The background color. See *yambar*(5) for format.

## EXAMPLES

```
content:
  string:
    deco:
      background:
        color: 00ff00ff
```

# UNDERLINE

This decoration renders a line of configurable size and color at the
bottom of the particle.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  size
:  int
:  yes
:  The size (height/thickness) of the line, in pixels
|  color
:  color
:  yes
:  The color of the line. See *yambar*(5) for format.

## EXAMPLES

```
content:
  string:
    deco:
      underline:
        size: 2
        color: ff0000ff
```


# BORDER

This decoration renders a border of configurable size (i.e border
width) around the particle.

## CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  color
:  color
:  yes
:  The color of the line. See *yambar*(5) for format.
|  size
:  int
:  no
:  Border width, in pixels. Defaults to 1px.

## EXAMPLES

```
content:
  string:
    deco:
      border:
        size: 2
        color: ff0000ff
```


# STACK

This particles combines multiple decorations.

## CONFIGURATION

No configuration attributes available. The _stack_ is just a *list* of
the decorations that should be combined.

## EXAMPLES

```
content:
  string:
    deco:
      stack:
        - background: ...
        - underline: ...
```

# SEE ALSO

*yambar-particles*(5)
