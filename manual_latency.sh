#!/bin/bash

make -j

mkdir -p log/latency

/mnt/ssd/flsm/lock_server.sh hao_tongliang
if [ `cat /mnt/ssd/flsm/lock` != "hao_tongliang" ]; then
    echo "Server lock acquiring failed: held by" `cat /mnt/ssd/flsm/lock`
    exit 1
fi

> log/latency/latency.log
dd if=/dev/zero of=tmp/file bs=2G count=1

for k in 4 8 16 32 64 128 256 512 1024 2048 4096; do
    for i in `seq 0 4`; do
        for l in `seq 0 6`; do
            sync
            sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"
            bin/latency 1 $i 1 31 $k $k 10 $l $l 2>&1 | tee -a log/latency/latency.log
        done
done done

/mnt/ssd/flsm/unlock_server.sh hao_tongliang
