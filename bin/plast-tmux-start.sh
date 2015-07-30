#!/bin/bash

FILE="$1"
if [ ! -x "$FILE" ]; then
    DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
    FILE="$DIR/$1"
    if [ ! -x "$FILE" ]; then
        echo "Can't find $1 ($FILE)"
        exit 1
    fi
fi
HELP="$0 <command> (--detach|-d|--attach|-a|--help|-h)"
shift
mode=detach
while [ -n "$1" ]; do
    case "$1" in
        --detach|-d)
            mode=detach
            ;;
        --attach|-a)
            mode=attach
            ;;
        --help|-h)
            echo "$HELP"
            exit 0
            ;;
        *)
            echo "$HELP"
            echo "Invalid switch $1"
            exit 1
            ;;
    esac
    shift
done

tmux new-session -s `basename $FILE` -d "$FILE" \; "$mode"

