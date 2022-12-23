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
:< *Description*
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
|  ssid
:  string
:  SSID the adapter is connected to (Wi-Fi only)
|  signal
:  int
:  Signal strength, in dBm (Wi-Fi only)
|  rx-bitrate
:  int
:  RX bitrate, in bits/s
|  tx-bitrate
:  int
:  TX bitrate in bits/s
|  dl-speed
:  int
:  Download speed in bits/s
|  ul-speed
:  int
:  Upload speed in bits/s


# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:< *Description*
|  name
:  string
:  yes
:  Name of network interface to monitor
|  poll-interval
:  int
:  no
:  Periodically (in milliseconds) update the signal, rx+tx bitrate, and
   ul+dl speed tags. Setting it to 0 disables updates. Cannot be less
   than 250ms.


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

