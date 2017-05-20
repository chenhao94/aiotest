#!/bin/bash

mkdir -p log/latency

if [ `hostname` == "erode-lom" ]; then
    /mnt/ssd/flsm/lock_server.sh hao_tongliang
    if [ `cat /mnt/ssd/flsm/lock` != "hao_tongliang" ]; then
        echo "Server lock acquiring failed: held by" `cat /mnt/ssd/flsm/lock`
        exit 1
    fi
fi

export ASAN_OPTIONS=use_odr_indicator=1

for i in 24 27 31; do 
    for k in 4 8 16 32 64 128 256 512 1024; do
        make test_mt TEST_ARGS=$i' '$k' '$k' 12 8 10'
done done

if [ `hostname` == "erode-lom" ]; then
    /mnt/ssd/flsm/unlock_server.sh hao_tongliang
fi
