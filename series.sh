#!/bin/bash

mkdir -p log

truncate -s0 cs.log

/mnt/ssd/flsm/lock_server.sh hao_tongliang
if [ `cat /mnt/ssd/flsm/lock`!="hao_tongliang" ]; then
    echo "Server lock acquiring failed: held by" `cat /mnt/ssd/flsm/lock`
    exit 1
fi

for t in `seq 3`; do 
    for k in 4 8 16 32 64 128 256 512 1024 2048 4096; do
        for i in `seq 0 4`; do
            for l in `seq 4`; do
                sync
                sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"
                sudo bash -c "perf stat -age cs bin/multi_thread_comp 4 $i 1 $k $k 31 10 8 8 2>&1" | tee -a cs.log
            done
            echo 'Context Switch:'
            grep '  cs' cs.log | sed 's/,//g' | sed 's/ *\([0-9]*\).*/\1/' | paste - - - - -
            echo 'Throughput (iops):'
            grep iops cs.log | sed 's/.* \([0-9\.]*\) iops/\1/' | paste - - - - -
done done done

/mnt/ssd/flyysm/unlock_server.sh hao_tongliang
