#!/bin/bash

function usage ()
{
    echo "update-plast.sh [...]"
    echo "  --server=<SERVER>"
    echo "  -s=<SERVER>"
    echo "  -s <SERVER>"
    echo "  --server <SERVER>"
    echo "  -s<SERVER>"
}

SERVER=
while [ -n "$1" ]; do
    case "$1" in
        --server=*|-s=*)
            SERVER=`echo $1 | sed -e 's,^.*=,,'`
            ;;
        --server|-s)
            shift
            SERVER="$1"
            ;;

        -s*)
            SERVER=`echo $1 | sed -e 's,^-s,,'`
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            usage
            echo "Invalid option $1"
            exit 1
            ;;
    esac
    shift
done

function install ()
{
    deb=`curl --silent $SERVER/apt/pool/main/p/$1/ | grep -o "$1[A-Za-z0-9_]*\.deb" | head -n1`
    if [ -z "$deb" ]; then
        echo "Can't parse output from $SERVER/apt/pool/main/p/$1"
        exit 1
    fi
    out=`mktemp -t plast-package-XXXX.deb`
    curl --silent  "$SERVER/apt/pool/main/p/$1/$deb" -o "$out"
    sudo dpkg -i "$out"
}
install plast32
install plast
