#!/bin/bash

FILE=
if echo "$1" | grep ^/ --quiet; then
    FILE=$1
else
    DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
    FILE="$DIR/$1"
fi
tmux new-session -s `basename $FILE` -d "$FILE" \; detach

