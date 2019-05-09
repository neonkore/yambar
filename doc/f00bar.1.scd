f00bar(1)

# NAME
f00bar - modular status panel for X11 and Wayland

# SYNOPSIS
*f00bar* [ -v ]

# OPTIONS

*-c, --check-config*
	Verify the configuration and then quit. If no errors are detected,
	nothing is printed and the exit code is 0. If there are errors,
	these are printed on stdout and the exit code is non-zero.

*-v, --version*
	Show the version number and quit

# DESCRIPTION
*f00bar* is a light-weight and configurable status panel (_bar_, for
short) for *X* and *Wayland*.

It has a number of _modules_ that provide information in the form of
_tags_. For example, the _clock_ module has a _date_ tag that contains
the current date.

The modules do not know how to present the information though. This is
instead done by _particles_. And the user, you, decides which
particles (and thus how to present the data) to use.

Furthermore, each particle can have a _decoration_. These are things
like a different background, or an graphical underline.

There is *no* support for images or icons. use an icon font
(e.g. *Font Awesome*, or *Material Icons*) if you want a graphical
representation.

There are a number of modules and particles builtin. More can be added
as plugins. You can even write your own!

# CONFIGURATION
See *f00bar*(5)
