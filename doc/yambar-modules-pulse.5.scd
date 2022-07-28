yambar-modules-pulse(5)

# NAME
pulse - Monitors a PulseAudio source and/or sink

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  online
:  bool
:  True when connected to the PulseAudio server
|  sink_online
:  bool
:  True when the sink is present
|  source_online
:  bool
:  True when the source is present
|  sink_percent
:  range
:  Sink volume level, as a percentage
|  source_percent
:  range
:  Source volume level, as a percentage
|  sink_muted
:  bool
:  True if the sink is muted, otherwise false
|  source_muted
:  bool
:  True if the source is muted, otherwise false
|  sink_port
:  string
:  Description of the active sink port
|  source_port
:  string
:  Description of the active source port

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  sink
:  string
:  no
:  Name of sink to monitor (default: _@DEFAULT\_SINK@_).
|  source
:  string
:  no
:  Name of source to monitor (default: _@DEFAULT\_SOURCE@_).

# EXAMPLES

```
bar:
  left:
    - pulse:
	    content: 
		  string: {text: "{sink_percent}% ({sink_port})"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

