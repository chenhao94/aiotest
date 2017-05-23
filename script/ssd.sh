#!/bin/bash

mkdir -p log

export ASAN_OPTIONS=use_odr_indicator=1

export TEST_TYPE='0 1 2 3 4 5 6'
export TEST_TYPE='0 2'

NAMES="\
4K-Overlay
4K-Discrete
64K-Overlay
64K-Discrete
1M-Overlay
1M-Discrete
"

NAMES="\
4K-Overlay
64K-Overlay
"

ARGS="\
25 4 4 19 16 16
34 4 4 19 16 16
27 64 64 17 14 14
34 64 64 17 14 14
27 1024 1024 13 10 10
35 1024 1024 13 10 10
"

ARGS="\
20 4 4 10 10 10
20 64 64 10 10 10
"

for i in $(seq $(expr $(wc -l <<<"$ARGS") - 1)); do
    rm -rf log/last
    if [ -e log/$(head -n$i <<<"$NAMES" | tail -n1) ]; then
        cp -rf log/$(head -n$i <<<"$NAMES" | tail -n1) log/last
    fi
    make test_mt TEST_ARGS="$(head -n$i <<<"$ARGS" | tail -n1)"
    rm -rf log/$(head -n$i <<<"$NAMES" | tail -n1)
    mv log/last log/$(head -n$i <<<"$NAMES" | tail -n1)
done

