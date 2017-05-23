#!/bin/bash

mkdir -p log

export ASAN_OPTIONS=use_odr_indicator=1

export TEST_TYPE='0 1 2 3 4 5 6'

if [ $1 == hdd ]; then
export TEST_TYPE='0 2 3 4 5 6'
fi

export NAMES="\
4K-Overlay
4K-Discrete
64K-Overlay
64K-Discrete
1M-Overlay
1M-Discrete
"

export ARGS="\
24 4 4 15 8 10
30 4 4 10 7 9
25 64 64 14 9 11
30 64 64 10 6 8
26 1024 1024 10 5 7
31 1024 1024 8 4 6
"

if [ $1 == ssd ]; then
export ARGS="\
25 4 4 19 17 17
34 4 4 19 17 17
27 64 64 17 15 15
34 64 64 17 15 15
27 1024 1024 13 11 11
35 1024 1024 13 11 11
"
fi

if [ $1 == hdd ]; then
export ARGS="\
24 4 4 17 15 15
29 4 4 12 10 10
25 64 64 14 12 12
29 64 64 11 9 9
26 1024 1024 12 9 9
31 1024 1024 9 7 7
"
fi

for i in $(seq $(expr $(wc -l <<<"$ARGS") - 1)); do
    rm -rf log/last
    if [ -e log/$(head -n$i <<<"$NAMES" | tail -n1) ]; then
        cp -rf log/$(head -n$i <<<"$NAMES" | tail -n1) log/last
    fi
    make test_mt TEST_ARGS="$(head -n$i <<<"$ARGS" | tail -n1)"
    rm -rf log/$(head -n$i <<<"$NAMES" | tail -n1)
    mv log/last log/$(head -n$i <<<"$NAMES" | tail -n1)
done

