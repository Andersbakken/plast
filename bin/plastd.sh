#!/bin/bash

while [ true ]; do
    `dirname ${BASH_SOURCE[0]}`/plastd $@
    if [ $? -ne 200 ]; then
        break
    fi
    pushd "$(git rev-parse --git-dir)/.."
    git stash
    git pull
    git stash pop
    if [ -e "build.ninja" ]; then
        ninja
    else
        make
    fi
    popd
done
