yambar-modules-mpd(5)

# NAME
mpd - This module provides MPD status such as currently playing artist/album/song

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  state
:  string
:  One of *offline*, *stopped*, *paused* or *playing*
|  repeat
:  bool
:  True if the *repeat* flag is set
|  random
:  bool
:  True if the *random* flag is set
|  consume
:  bool
:  True if the *consume* flag is set
|  volume
:  range
:  Volume of MPD in percentage
|  album
:  string
:  Currently playing album (also valid in *paused* state)
|  artist
:  string
:  Artist of currently playing song (also valid in *paused* state)
|  title
:  string
:  Title of currently playing song (also valid in *paused* state)
|  pos
:  string
:  *%M:%S*-formatted string describing the song's current position
   (also see _elapsed_)
|  end
:  string
:  *%M:%S*-formatted string describing the song's total length (also
   see _duration_)
|  elapsed
:  realtime
:  Position in currently playing song, in milliseconds. Can be used
   with a _progress-bar_ particle.
|  duration
:  int
:  Length of currently playing song, in milliseconds

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  host
:  string
:  yes
:  Hostname/IP/unix-socket to connect to
|  port
:  int
:  no
:  TCP port to connect to

# EXAMPLES

```
bar:
  left:
    - mpd:
        host: /run/mpd/socket
        content:
          string: {text: "{artist} - {album} - {title} ({end})"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

