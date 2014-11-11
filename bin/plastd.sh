#!/bin/bash

while [ true ]; do
    dir=`dirname ${BASH_SOURCE[0]}`
    "$dir/plastd" $@
    if [ $? -ne 200 ]; then
        break
    fi

    pushd "$dir"
    pushd "$(git rev-parse --git-dir)/.."
    if git stash; then
        stashed=1
    fi
    git pull --recurse-submodules=yes
    if [ -n "$stashed" ]; then
        git stash pop
    fi
    if [ -e "build.ninja" ]; then
        ninja
    else
        make
    fi
    popd
    popd
done
