#!/usr/bin/env bash
#
# pacman.sh - display number of packages update available
#             by default check every hour
#
# USAGE: pacman.sh
#
# TAGS:
#  Name      Type    Return
#  -------------------------------------------
#  {pacman}  string  number of pacman packages
#  {aur}     string  number of aur packages
#  {pkg}     string  sum of both
#
# Exemple configuration:
#  - script:
#    path: /absolute/path/to/pacman.sh
#    args: [] 
#    content: { string: { text: " {pacman} + {aur} = {pkg}" } }


declare interval no_update aur_helper pacman_num aur_num pkg_num

# Error message in STDERR
_err() {
  printf -- '%s\n' "[$(date +'%Y-%m-%d %H:%M:%S')]: $*" >&2
}


while true; do
  # Change interval
  # NUMBER[SUFFIXE]
  # Possible suffix:
  #  "s" seconds / "m" minutes / "h" hours / "d" days 
  interval="1h"
  
  # Change the message you want when there is no update
  # Leave empty if you want a 0 instead of a string 
  # (e.g. no_update="")
  no_update="no update"
  
  # Change your aur manager
  aur_helper="paru"

  # Get number of packages to update
  pacman_num=$(checkupdates | wc -l)

  if ! hash "${aur_helper}" >/dev/null 2>&1; then
    _err "aur helper not found, change it in the script"
  else
    aur_num=$("${aur_helper}" -Qmu | wc -l)
  fi
  
  pkg_num=$(( pacman_num + aur_num ))

  # Only display one if there is no update and multiple tags set
  if [[ "${pacman_num}" == 0 && "${aur_num}" == 0 ]]; then
    pacman_num="${no_update:-$pacman_num}"
    aur_num="${no_update:-$aur_num}"
    pkg_num="${no_update:-$pkg_num}"

    printf -- '%s\n' "pacman|string|"
    printf -- '%s\n' "aur|string|"
    printf -- '%s\n' "pkg|string|${pkg_num}"
    printf -- '%s\n' ""
  else 
    printf -- '%s\n' "pacman|string|${pacman_num}"
    printf -- '%s\n' "aur|string|${aur_num}"
    printf -- '%s\n' "pkg|string|${pkg_num}"
    printf -- '%s\n' ""
  fi

  sleep "${interval}"

done

unset -v interval no_update aur_helper pacman_num aur_num pkg_num
unset -f _err

