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
#  Name      Type    Return
#  -------------------------------------
#  {dwltag}  string  dwl tags name/state
#
# Exemple configuration:
#  - script:
#      path: /absolute/path/to/dwl-tags.sh
#      args: [1]
#      content: {string: {text: "{dwltag}"}}


# Variables
declare titleline tagline title taginfo isactive ctags mtags layout
declare symbol_occupied_pre symbol_occupied_post symbol_focused_pre symbol_focused_post
declare -a tags name
readonly fname=/tmp/dwltags-"$WAYLAND_DISPLAY"


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

  tags=( "1" "2" "3" "4" "5" "6" "7" "8" "9" )

  # Name of tag (optional)
  # If there is no name, number are used
  #
  # Example:
  #  name=( "" "" "" "Media" )
  #  -> return "" "" "" "Media" 5 6 7 8 9)
  name=()

  # Symbol for occupied tags
  #
  # Format: "{symbol_occupied_pre}{TAGNAME}{symbol_occupied_post}"
  # You can leave one empty if you don't want to surround the TAGNAME
  symbol_occupied_pre=""
  symbol_occupied_post="."

  # Symbol for the focused tag
  #
  # Format: "{symbol_focused_pre}{TAGNAME}{symbol_focused_post}"
  # You can leave one empty if you don't want to surround the TAGNAME
  symbol_focused_pre="[ "
  symbol_focused_post=" ]"

  for i in {0..8}; do
    mask=$((1<<i))
    n="$((tags[i]++))"

    # Occupied
    if (( "${ctags}" & mask )); then
      name[i]="${symbol_occupied_pre}${name[i]:-${n}}${symbol_occupied_post}"
      tags[i]="${name[i]}"
    else
      tags[i]="${name[i]:-${n}}"
    fi
    
    # Focused
    if (( "${mtags}" & mask )); then
      tags[i]="${symbol_focused_pre}${name[i]:-${n}}${symbol_focused_post}"
    else
      tags[i]=" ${name[i]:-${n}} "
    fi

  done
  
  # Format send to yambar
  #
  # tags[*]  - array of tags name
  # layout   - dwl layout (tile, floating, monocle) (optional)
  # title    - name of the focused client (optional)
  fmt="${tags[*]} ${layout} [ ${title:-1:50} ]" # Limit title to 50 caracters

  printf -- '%s\n' "dwltag|string|${fmt}"
  printf -- '%s\n' ""

done

unset -v titleline tagline title taginfo isactive ctags mtags layout 
unset -v symbol_occupied_pre symbol_occupied_post symbol_focused_pre symbol_focused_post
unset -v tags name
