#!/bin/bash

mkdir -p log

truncate -s0 cs.log

for k in `seq 12 20`; do
    for i in `seq 0 4`; do
        sync
        sudo bash -c "echo 1 > /proc/sys/vm/drop_caches"
        sudo bash -c "perf stat -age cs bin/multi_thread_comp 4 $i 1 31 $k $k 10 8 8 2>&1" | tee -a cs.log
    done
    echo 'Context Switch:'
    grep '  cs' cs.log | sed 's/,//g' | sed 's/ *\([0-9]*\).*/\1/' | paste - - - - -
    echo 'Throughput (iops):'
    grep iops cs.log | sed 's/.* \([0-9\.]*\) iops/\1/' | paste - - - - -
done
