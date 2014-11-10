#!/bin/bash

while [ true ]; do
    `dirname ${BASH_SOURCE[0]}`/plastd $@
    if [ $? -ne 200 ]; then
        break
    fi
    pushd "$(git rev-parse --git-dir)/.."
    if git stash; then
        stashed=1
    fi
    git pull
    if [ -n "$stashed" ]; then
        git stash pop
    fi
    if [ -e "build.ninja" ]; then
        ninja
    else
        make
    fi
    popd
done
