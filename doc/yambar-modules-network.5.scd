yambar-modules-network(5)

# NAME
network - This module monitors network connection state

# DESCRIPTION

This module monitors network connection state; disconnected/connected
state and MAC/IP addresses.

Note: while the module internally tracks all assigned IPv4/IPv6
addresses, it currently exposes only a single IPv4 and a single IPv6
address.

# TAGS

[[ *Name*
:[ *Type*
:[ *Description*
|  name
:  string
:  Network interface name
|  index
:  int
:  Network interface index
|  carrier
:  bool
:  True if the interface has CARRIER. That is, if it is physically connected.
|  state
:  string
:  One of *unknown*, *not present*, *down*, *lower layers down*,
   *testing*, *dormant* or *up*. You are probably interested in *down* and *up*.
|  mac
:  string
:  MAC address
|  ipv4
:  string
:  IPv4 address assigned to the interface, or *""* if none
|  ipv6
:  string
:  IPv6 address assigned to the interface, or *""* if none

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:[ *Description*
|  name
:  string
:  Name of network interface to monitor

# EXAMPLES

```
bar:
  left:
    - network:
        name: wlp3s0
        content:
          string: {text: "{name}: {state} ({ipv4})"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

