f00bar-modules(5)

# NAME
f00bar-modules - configuration file

# DESCRIPTION

Modules are what monitors your system and provides data for the status
bar.

All modules expose their data through *tags*. Each tag has a *name*,
*type* and *value*. The name and type is fixed, while the value
typically changes over time. See *TAGS*.

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

# TAGS

Tags are the data carriers; it is through tags that modules expose
their information. Each module defines its own set of tags.

The available tag *types* are:

[[ *Type*
:[ *Description*
|  string
:  Value is a string. Rendered as-is by the _string_ particle.
|  int
:  Value is an integer. Rendered in base 10 by the _string_ particle.
|  bool
:  Value is true or false. Rendered as "true" or "false" by the _string_
   particle
|  float
:  Value is a float. Rendered in base 10, with two decimal digits by the
   _string_ particle
|  range
:  Value is an integer, with a minimum and maximum value associated
   with it. By default, the _string_ particle renders the value. The
   _:min_ or _:max_ suffixes by be added to instead render the mininum
   or maximum value (_\"{tag_name:min}\"_).
|  realtime
:  Value is an integer that changes in a predictable manner (in
   "realtime"). This allows the particle to update itself
   periodically. Only supported by the
   *f00bar-particle-progress-bar*(5). Other particles can still render
   the tag's value. And, the _string_ particle recognizes the _:unit_
   suffix, which will be translated to a "s" for a tag with "seconds"
   resolution, or "ms" for one with "milliseconds" resolution.

# MODULES

- *f00bar-module-alsa*(5)
