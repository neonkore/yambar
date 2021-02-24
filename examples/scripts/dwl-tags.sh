#!/usr/bin/env bash
#
# dwl-tags.sh - display dwl tags
#
# USAGE: dwl-tags.sh 1
#
# REQUIREMENTS:
#  - inotifywait ( 'inotify-tools' on arch )
#  - 2021/02/22 - dwl pull request:
#    'Interface to display tag information on status bar #91'
#    https://github.com/djpohly/dwl/pull/91
#
# TAGS:
#  Name                 Type    Return
#  ----------------------------------------------------
#  {dwltag_N}           string  dwl tags name
#  {dwltag_N_occupied}  bool    dwl tags state occupied
#  {dwltag_N_focused}   bool    dwl tags state focused
#  {dwl_layout}         string  dwl layout
#  {dwl_title}          string  client title
#
# Now the fun part
#
# Exemple configuration:
#
#     - script:
#         path: /absolute/path/to/dwl-tags.sh
#         args: [1]
#         anchors:
#           - occupied: &occupied {foreground: 57bbf4ff}
#           - focused: &focused {foreground: fc65b0ff}
#           - default: &default {foreground: d2ccd6ff}
#         content:
#           - map:
#               margin: 4
#               tag: dwltag_0_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_0_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_0}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_0}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_0_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_0}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_0}", <<: *default}
#           - map:
#               margin: 4
#               tag: dwltag_1_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_1_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_1}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_1}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_1_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_1}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_1}", <<: *default}
#           - map:
#               margin: 4
#               tag: dwltag_2_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_2_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_2}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_2}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_2_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_2}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_2}", <<: *default}
#           - map:
#               margin: 4
#               tag: dwltag_3_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_3_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_3}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_3}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_3_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_3}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_3}", <<: *default}
#           - map:
#               margin: 4
#               tag: dwltag_4_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_4_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_4}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_4}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_4_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_4}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_4}", <<: *default}
#           - map:
#               margin: 4
#               tag: dwltag_5_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_5_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_5}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_5}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_5_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_5}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_5}", <<: *default}
#           - map:
#               margin: 4
#               tag: dwltag_6_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_6_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_6}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_6}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_6_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_6}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_6}", <<: *default}
#           - map:
#               margin: 4
#               tag: dwltag_7_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_7_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_7}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_7}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_7_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_7}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_7}", <<: *default}
#           - map:
#               margin: 4
#               tag: dwltag_8_occupied
#               values:
#                 true:
#                   map:
#                     tag: dwltag_8_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_8}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_8}", <<: *occupied}
#                 false:
#                   map:
#                     tag: dwltag_8_focused
#                     values:
#                       true:
#                           - string: {text: "{dwltag_8}", <<: *focused}
#                       false:
#                           - string: {text: "{dwltag_8}", <<: *default}
#           - list:
#               spacing: 3
#               items:
#                   - string: {text: "{dwl_layout}"}
#                   - string: {text: "{dwl_title}"}


# Variables
declare titleline tagline title taginfo isactive ctags mtags layout
declare -a tags name
readonly fname=/tmp/dwltags-"$WAYLAND_DISPLAY"


_cycle() {
  tags=( "1" "2" "3" "4" "5" "6" "7" "8" "9" )

  # Name of tag (optional)
  # If there is no name, number are used
  #
  # Example:
  #  name=( "" "" "" "Media" )
  #  -> return "" "" "" "Media" 5 6 7 8 9)
  name=()

  for tag in "${!tags[@]}"; do
    mask=$((1<<tag))

    tag_name="dwltag"
    declare "${tag_name}_${tag}"
    name[tag]="${name[tag]:-${tags[tag]}}"

    printf -- '%s\n' "${tag_name}_${tag}|string|${name[tag]}"

    # Occupied
    if (( "${ctags}" & mask )); then
      printf -- '%s\n' "${tag_name}_${tag}_occupied|bool|true"
    else
      printf -- '%s\n' "${tag_name}_${tag}_occupied|bool|false"
    fi

    # Focused
    if (( "${mtags}" & mask )); then
      printf -- '%s\n' "${tag_name}_${tag}_focused|bool|true"
      printf -- '%s\n' "dwl_title|string|${title}"
    else
      printf -- '%s\n' "${tag_name}_${tag}_focused|bool|false"
    fi

  done

  printf -- '%s\n' "dwl_layout|string|${layout}"
  printf -- '%s\n' ""

}

# Call the function here so the tags are displayed at dwl laucnh
_cycle

while true; do

  # Make sure the file exists
  while [ ! -f "${fname}" ]; do
    inotifywait -qqe create "$(dirname "${fname}")"
  done;

  # Wait for dwl to close it after writing
  inotifywait -qqe close_write "${fname}"

  # Get info from the file
  titleline="$1"
  tagline=$((titleline+1))
  title=$(sed "${titleline}!d" "${fname}")
  taginfo=$(sed "${tagline}!d" "${fname}")
  isactive=$(printf -- '%s\n' "${taginfo}" | cut -d ' ' -f 1)
  ctags=$(printf -- '%s\n' "${taginfo}" | cut -d ' ' -f 2)
  mtags=$(printf -- '%s\n' "${taginfo}" | cut -d ' ' -f 3)
  layout=$(printf -- '%s\n' "${taginfo}" | cut -d ' ' -f 4-)

  _cycle

done

unset -v titleline tagline title taginfo isactive ctags mtags layout 
unset -v tags name

