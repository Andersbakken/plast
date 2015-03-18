#!/bin/bash

function usage ()
{
    echo "create-package.sh"
    echo "  --help|-h"
    echo "  --repo=...|-r=..."
    echo "  --no-build|-n"
    echo "  --build-flags|-b"
}
BUILD=1
REPO=
BUILD_FLAGS="$MAKEFLAGS"
while [ -n "$1" ]; do
    case "$1" in
        --repo=*|-r=*)
            REPO=`echo $1 | sed -e 's,^.*=,,'`
            ;;
        --no-build|-n)
            BUILD=0
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --build-flags=*|-b=*)
            BUILD_FLAGS=`echo $1 | sed -e 's,^.*=,,'`
            ;;
        *)
            usage
            echo "Unknow argument \"$1\""
            exit 1
            ;;
    esac
    shift
done

if [ -z "$REPO" ]; then
    usage
    echo No repo. Use -r=|--repo=
    exit 1
fi

GITROOT=
if [ "$BUILD" = "1" ]; then
    cd `git rev-parse --git-dir`/..
    GITROOT=`pwd`
    if [ ! -e build.ninja ] && [ ! -e Makefile ]; then
        if [ -x `which ninja` ]; then
            cmake -G Ninja .
        else
            cmake .
        fi
    fi
    if [ -e build.ninja ]; then
        ninja $BUILD_FLAGS
    elif [ -e Makefile ]; then
        make $BUILD_FLAGS
    fi
fi

dir=`mktemp -t plast-XXXXX`
cd "$dir"
git clone $repo .
PREFIX="$PWD/usr/local/plast/"
git rm -rf "$PREFIX"
cp -r "$GITROOT/bin/plastc $GITROOT/bin/plastd $GITROOT/bin/plasts GITROOT/bin/plast-prefix.sh $GITROOT/src/plastsh/plastsh.js $GITROOT/src/plastsh/packages.json $GITROOT/src/plasts/stats" "$PREFIX"
CURRENTVERSION=`grep ^Version: ./DEBIAN/control | sed -e 's,Version: ,,'`
VERSION=`expr $CURRENTVERSION + 1`
sed -i -e "s,Version: $CURRENTVERSION,Version: $VERSION," ./DEBIAN/control
git add .
git commit -m "Bumped plast to version: $VERSION"




