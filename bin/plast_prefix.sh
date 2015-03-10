#!/bin/bash
export PLAST_COMPILER="$1"
shift
if [ -n "$PLAST_DISABLED" ]; then
    "$PLAST_COMPILER" "$@"
else
    SOURCE="${BASH_SOURCE[0]}"
    while [ -h "$SOURCE" ]; do # resolve $SOURCE until the file is no longer a symlink
        DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"
        SOURCE="$(readlink "$SOURCE")"
        [[ $SOURCE != /* ]] && SOURCE="$DIR/$SOURCE"
    done
    DIR="$( cd -P "$( dirname "$SOURCE" )" && pwd )"

    "$DIR/plastc" "$@"
fi
exit $?
