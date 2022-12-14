yambar-modules-script(5)

# NAME
script - This module executes a user-provided script (or binary!)

# DESCRIPTION

This module executes a user-provided script (or binary!) that writes
tags on its stdout.

Scripts can be run in two modes: yambar polled, or continuously. In the
yambar polled mode, the script is expected to write one set of tags
and then exit. Yambar will execute the script again after a
configurable amount of time.

In continuous mode, the script is executed once. It will typically run
in a loop, sending an updated tag set whenever it needs, or wants
to. The last tag set is used (displayed) by yambar until a new tag set
is received. This mode is intended to be used by scripts that depends
on non-polling methods to update their state.

Tag sets, or _transactions_, are separated by an empty line
(e.g. *echo ""*). The empty line is required to commit (update) the
tag even for only one transaction.

Each _tag_ is a single line on the format:

```
name|type|value
```

Where _name_ is what you also use to refer to the tag in the yambar
configuration, _type_ is one of the tag types defined in
*yambar-tags*(5), and _value_ is the tag’s value.

Example:

```
var1|string|hello
var2|int|13
  <empty>
var1|string|world
var2|int|37
  <empty>
```

The example above consists of two transactions. Each transaction has
two tags: one string tag and one integer tag. The second transaction
replaces the tags from the first transaction. Note that **both**
transactions need to be terminated with an empty line.

Supported _types_ are:

- string
- int
- bool
- float
- range:n-m (e.g. *var|range:0-100|57*)

# TAGS

User defined.

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:< *Description*
|  path
:  string
:  yes
:  Path to script/binary to execute. Must be an absolute path.
|  args
:  list of strings
:  no
:  Arguments to pass to the script/binary.
|  poll-interval
:  integer
:  no
:  Number of seconds between each script run. If unset, continuous mode
   is used.

# EXAMPLES

Here is an "hello world" example script:

```
#!/bin/sh

while true; do
    echo "test|string|hello"
    echo ""
    sleep 3

    echo "test|string|world"
    echo ""
    sleep 3
done
```

This script runs in continuous mode, and will emit a single string tag,
_test_, and alternate its value between *hello* and *world* every
three seconds.

A corresponding yambar configuration could look like this:

```
bar:
  left:
    - script:
        path: /path/to/script.sh
        args: []
        content: {string: {text: "{test}"}}
```

Another example use case of this module could be to display currently playing
song or other media from players that support MPRIS (Media Player Remote
Interfacing Specification):

```
bar:
  center:
    - script:
        path: /usr/bin/playerctl
        args:
          - "--follow"
          - "metadata"
          - "-f"
          - |
            status|string|{{status}}
            artist|string|{{artist}}
            title|string|{{title}}
        content:
          map:
            conditions:
              status == Paused: {empty: {}}
              status == Playing:
                content: {string: {text: "{artist} - {title}"}}
```

The above snippet runs a _playerctl_ utility in _--follow_ mode, reacting to
media updates on DBUS and outputting status, artist and title of media being
played in a format that is recognized by yambar. See _playerctl_ documentation
for more available metadata fields and control over which players get used.

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

