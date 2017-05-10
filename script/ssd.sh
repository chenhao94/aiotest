#!/bin/bash

mkdir -p log

if [ `hostname` == erode-lom ]; then
    /mnt/ssd/flsm/lock_server.sh hao_tongliang
    if [ `cat /mnt/ssd/flsm/lock` != "hao_tongliang" ]; then
        echo "Server lock acquiring failed: held by" `cat /mnt/ssd/flsm/lock`
        exit 1
    fi
fi

export ASAN_OPTIONS=use_odr_indicator=1

export TEST_TYPE='6'

make test_mt TEST_ARGS='25 4 4 19 16 16' 2>&1 | tee log/4K-Overlay.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='34 4 4 19 16 16' 2>&1 | tee log/4K-Discrete.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='27 64 64 17 14 14' 2>&1 | tee log/64K-Overlay.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='34 64 64 17 14 14' 2>&1 | tee log/64K-Discrete.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='27 1024 1024 13 10 10' 2>&1 | tee log/1M-Overlay.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='35 1024 1024 13 10 10' 2>&1 | tee log/1M-Discrete.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

if [ `hostname` == erode-lom ]; then
    /mnt/ssd/flsm/unlock_server.sh hao_tongliang
fi
