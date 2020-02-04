yambar(1)

# NAME
yambar - modular status panel for X11 and Wayland

# SYNOPSIS
*yambar* [_OPTIONS_]...

# OPTIONS

*-b*,*--backend*={*xcb*,*wayland*,*auto*}
	Backend to use. The default is *auto*. In this mode, yambar will
	look for the environment variable _WAYLAND\_DISPLAY_, and if
	available, use the *Wayland* backend. If not, the *XCB* backend is
	used.

*-c*,*--config*=_FILE_
	Use an alternative configuration file instead of the default one.

*-C*,*--validate*
	Verify the configuration and then quit. If no errors are detected,
	nothing is printed and the exit code is 0. If there are errors,
	these are printed on stdout and the exit code is non-zero.

*-p*,*--print-pid*=_FILE_|_FD_
	Print PID to this file, or FD, when successfully started. The file
	(or FD) is closed immediately after writing the PID. When a _FILE_
	as been specified, the file is unlinked when yambar exits.

*-v*,*--version*
	Show the version number and quit

# DESCRIPTION
*yambar* is a lightweight and configurable status panel (_bar_, for
short) for *X11* and *Wayland*.

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
See *yambar*(5)
