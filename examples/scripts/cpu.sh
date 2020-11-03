#!/bin/bash

# cpu.sh - measures CPU usage at a configurable sample interval
#
# Usage: cpu.sh INTERVAL_IN_SECONDS
#
# This script will emit the following tags on stdout (N is the number
# of logical CPUs):
#
#  Name   Type
#  --------------------
#  cpu    range 0-100
#  cpu0   range 0-100
#  cpu1   range 0-100
#  ...
#  cpuN-1 range 0-100
#
# I.e. ‘cpu’ is the average (or aggregated) CPU usage, while cpuX is a
# specific CPU’s usage.
#
# Example configuration (update every second):
#
#  - script:
#      path: /path/to/cpu.sh
#      args: [1]
#      content: {string: {text: "{cpu}%"}}
#

interval=${1}

case ${interval} in
    ''|*[!0-9]*)
        echo "interval must be an integer"
        exit 1
        ;;
    *)
        ;;
esac

# Get number of CPUs, by reading /proc/stat
# The output looks like:
#
#  cpu  A B C D ...
#  cpu0 A B C D ...
#  cpu1 A B C D ...
#  cpuN A B C D ...
#
# The first line is a summary line, accounting *all* CPUs
IFS=$'\n' readarray -t all_cpu_stats < <(grep -e "^cpu" /proc/stat)
cpu_count=$((${#all_cpu_stats[@]} - 1))

# Arrays of ‘previous’ idle and total stats, needed to calculate the
# difference between each sample.
prev_idle=()
prev_total=()
for i in $(seq ${cpu_count}); do
    prev_idle+=(0)
    prev_total+=(0)
done

prev_average_idle=0
prev_average_total=0

while true; do
    IFS=$'\n' readarray -t all_cpu_stats < <(grep -e "^cpu" /proc/stat)

    usage=()           # CPU usage in percent, 0 <= x <= 100
    idle=()            # idle time since boot, in jiffies
    total=()           # total time since boot, in jiffies

    average_idle=0  # All CPUs idle time since boot
    average_total=0 # All CPUs total time since boot

    for i in $(seq 0 $((cpu_count - 1))); do
        # Split this CPUs stats into an array
        stats=($(echo "${all_cpu_stats[$((i + 1))]}"))

        # Clear (zero out) “cpuN”
        unset "stats[0]"

        # CPU idle time since boot
        idle[i]=${stats[4]}
        average_idle=$((average_idle + idle[i]))

        # CPU total time since boot
        total[i]=0
        for v in "${stats[@]}"; do
            total[i]=$((total[i] + v))
        done
        average_total=$((average_total + total[i]))

        # Diff since last sample
        diff_idle=$((idle[i] - prev_idle[i]))
        diff_total=$((total[i] - prev_total[i]))

        usage[i]=$((100 * (diff_total - diff_idle) / diff_total))

        prev_idle[i]=${idle[i]}
        prev_total[i]=${total[i]}
    done

    diff_average_idle=$((average_idle - prev_average_idle))
    diff_average_total=$((average_total - prev_average_total))

    average_usage=$((100 * (diff_average_total - diff_average_idle) / diff_average_total))

    prev_average_idle=${average_idle}
    prev_average_total=${average_total}

    echo "cpu|range:0-100|${average_usage}"
    for i in $(seq 0 $((cpu_count - 1))); do
        echo "cpu${i}|range:0-100|${usage[i]}"
    done

    echo ""
    sleep "${interval}"
done
