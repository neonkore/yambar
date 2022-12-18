yambar-modules-disk-io(5)

# NAME
disk-io - This module keeps track of the amount of bytes being
read/written from/to disk. It can distinguish between all partitions
currently present in the machine.

# TAGS

[[ *Name*
:[ *Type*
:< *Description*
|  device
:  string
:  Name of the device being tracked (use the command *lsblk* to see these).
   There is a special device, "Total", that reports the total stats
   for the machine
|  is_disk
:  boolean
:  whether or not the device is a disk (e.g. sda, sdb) or a partition
   (e.g. sda1, sda2, ...). "Total" is advertised as a disk.
|  read_speed
:  int
:  bytes read, in bytes/s
|  write_speed
:  int
:  bytes written, in bytes/s
|  ios_in_progress
:  int
:  number of ios that are happening at the time of polling

# CONFIGURATION

[[ *Name*
:[ *Type*
:[ *Req*
:< *Description*
|  poll-interval
:  int
:  no
:  Refresh interval of disk's stats in ms (default=500).
   Cannot be less then 500 ms

# EXAMPLES

This reports the total amount of bytes being read and written every second,
formatting in b/s, kb/s, mb/s, or gb/s, as appropriate.

```
bar:
  left:
    - disk-io:
        poll-interval: 1000
        content:
          map:
            conditions:
              device == Total:
                list:
                  items:
                    - string: {text: "Total read: "}
                    - map:
                        default: {string: {text: "{read_speed} B/s"}}
                        conditions:
                          read_speed > 1073741824:
                            string: {text: "{read_speed:gib} GB/s"}
                          read_speed > 1048576:
                            string: {text: "{read_speed:mib} MB/s"}
                          read_speed > 1024:
                            string: {text: "{read_speed:kib} KB/s"}
                    - string: {text: " | "}
                    - string: {text: "Total written: "}
                    - map:
                        default: {string: {text: "{write_speed} B/s"}}
                        conditions:
                          write_speed > 1073741824:
                            string: {text: "{write_speed:gib} GB/s"}
                          write_speed > 1048576:
                            string: {text: "{write_speed:mib} MB/s"}
                          write_speed > 1024:
                            string: {text: "{write_speed:kib} KB/s"}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)
