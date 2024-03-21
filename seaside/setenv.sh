#!/bin/bash

if [[ -z "$ICP_ROOT" ]]; then
    echo "You need to set ICP_ROOT yourself"
    return
fi

export TEAM_NAME="DCG SV"
export SITE_NAME=${TEAM_NAME}
export QAT=${ICP_ROOT}
export KERNEL_SOURCE_ROOT=/lib/modules/$(uname -r)
export OS_LEVEL=linux
export PROJ=CPM18
#export PROJ=COLETO_CREEK
