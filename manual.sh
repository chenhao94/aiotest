#!/bin/bash

make -j

mkdir -p log

/mnt/ssd/flsm/lock_server.sh hao_tongliang
if [ `cat /mnt/ssd/flsm/lock`!="hao_tongliang" ]; then
    echo "Server lock acquiring failed: held by" `cat /mnt/ssd/flsm/lock`
    exit 1
fi

make test_mt TEST_ARGS='4 4 24 17 8 10' 2>&1 | tee log/4K-Overlay.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='4 4 30 13 8 10' 2>&1 | tee log/4K-Discrete.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='64 64 26 14 8 10' 2>&1 | tee log/64K-Overlay.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS=' 64 64 30 10 6 8' 2>&1 | tee log/64K-Discrete.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='1024 1024 26 10 5 7' 2>&1 | tee log/1M-Overlay.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

make test_mt TEST_ARGS='1024 1024 31 9 4 6' 2>&1 | tee log/1M-Discrete.log
for i in `ls log/*.log`; do echo $i; head -n11 $i | tail -n+2; grep iops $i | sed 's/:.*, /:#/' | sed 's/#\([0-9][0-9]\.\)/# \1/' | column -s\# -t; echo; echo; done

/mnt/ssd/flyysm/unlock_server.sh hao_tongliang
