#!/bin/bash

function usage ()
{
    echo "create-package.sh"
    echo "  --help|-h"
    echo "  --repo=...|-r=..."
    echo "  --no-build|-n"
    echo "  --no-push|-p"
    echo "  --build-flags|-b"
    echo "  --message=...|-m=..."
}
BUILD=1
PUSH=1
REPO=
BUILD_FLAGS="$MAKEFLAGS"
MESSAGE=
while [ -n "$1" ]; do
    case "$1" in
        --repo=*|-r=*)
            REPO=`echo $1 | sed -e 's,^.*=,,'`
            ;;
        --no-build|-n)
            BUILD=0
            ;;
        --no-push|-p)
            PUSH=0
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --build-flags=*|-b=*)
            BUILD_FLAGS=`echo $1 | sed -e 's,^.*=,,'`
            ;;
        --message=*|-m=*)
            MESSAGE=`echo $1 | sed -e 's,^.*=,,'`
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

cd `git rev-parse --git-dir`/..
GITROOT=`pwd`
if [ "$BUILD" = "1" ]; then
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

dir=`mktemp -d -t plast-XXXXX`
echo "cd $dir"
cd "$dir"
git clone "$REPO" .
PREFIX="$PWD/plast32/usr/bin"
SERVER_PREFIX="$PWD/plast-server32/usr/bin"
git rm -rf "$PREFIX" "$SERVER_PREFIX" 2>/dev/null
mkdir -p "$PREFIX"
mkdir -p "$SERVER_PREFIX"
cp -r "$GITROOT/bin/plastc" "$GITROOT/bin/plastd" "$GITROOT/bin/plast_prefix.sh" "$GITROOT/bin/plast-tmux-start.sh" "$GITROOT/bin/plastd-tmux-start.sh" "$GITROOT/bin/plasts-tmux-start.sh""$PREFIX"
cp -r "$GITROOT/bin/plasts" "$SERVER_PREFIX"

find "$GITROOT/bin/stats/" -type f \( -name "*.css" -or -name "*.js" -or -name "*.html" \) | while read i; do
    target=`echo $i | sed -e "s,^$GITROOT/bin/,$SERVER_PREFIX,"`
    mkdir -p "`dirname \"$target\"`"
    cp "$i" "$target"
done
CURRENTVERSION=`grep ^Version: ./plast/DEBIAN/control | sed -e 's,Version: ,,'`
VERSION=`expr $CURRENTVERSION + 1`
sed -i -e "s,Version: $CURRENTVERSION,Version: $VERSION," ./plast/DEBIAN/control ./plast32/DEBIAN/control ./plast-server/DEBIAN/control ./plast-server32/DEBIAN/control
git add .
[ -z "$MESSAGE" ] && MESSAGE="Bumped plast to version: $VERSION"
git commit -m "$MESSAGE"
if [ "$PUSH" = "1" ]; then
    git push origin
fi
