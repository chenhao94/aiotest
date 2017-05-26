#!/bin/bash

mkdir -p log

export ASAN_OPTIONS=use_odr_indicator=1

export TEST_TYPE='0 1 2 3 4 5 6'

export NAMES="\
Transaction
"

export ARGS="\
30 2048 2048 8 8 8
"

for i in $(seq $(expr $(wc -l <<<"$ARGS") - 1)); do
    rm -rf log/last
    if [ -e log/$(head -n$i <<<"$NAMES" | tail -n1) ]; then
        cp -rf log/$(head -n$i <<<"$NAMES" | tail -n1) log/last
    fi
    make test_tx TEST_ARGS="$(head -n$i <<<"$ARGS" | tail -n1)"
    rm -rf log/$(head -n$i <<<"$NAMES" | tail -n1)
    mv log/last log/$(head -n$i <<<"$NAMES" | tail -n1)
done

