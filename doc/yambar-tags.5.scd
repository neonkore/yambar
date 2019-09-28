yambar-tags(5)

# NAME
yambar-tags - configuration file

# DESCRIPTION

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
   *yambar-particle-progress-bar*(5). Other particles can still render
   the tag's value. And, the _string_ particle recognizes the _:unit_
   suffix, which will be translated to a "s" for a tag with "seconds"
   resolution, or "ms" for one with "milliseconds" resolution.

