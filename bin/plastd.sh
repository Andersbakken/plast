#!/bin/bash

while [ true ]; do
    ${BASH_SOURCE[0]}/plastd $@
    if [ $? -ne 666 ]; then
        break
    fi
done
