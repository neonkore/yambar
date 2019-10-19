#!/usr/bin/bash

set -e

default_version=${1}
src_dir=${2}
out_file=${3}

# echo "default version:  ${default_version}"
# echo "source directory: ${src_dir}"
# echo "output file:      ${out_file}"

if [[ $(command -v git) ]]; then
    new_version="$(git describe --always --tags) ($(env LC_TIME=C date "+%b %d %Y"), branch '$(git rev-parse --abbrev-ref HEAD)')"
else
    new_version="${default_version}"
fi

new_version="#define YAMBAR_VERSION \"${new_version}\""

if [[ -f "${out_file}" ]]; then
    old_version=$(<"${out_file}")
else
    old_version=""
fi

# echo "old version: ${old_version}"
# echo "new version: ${new_version}"

if [[ "${old_version}" != "${new_version}" ]]; then
    echo "${new_version}" > "${out_file}"
fi
