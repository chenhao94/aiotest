#!/bin/bash

mkdir -p log/1thread/near256

if [ `hostname` == erode-lom ]; then
    /mnt/ssd/flsm/lock_server.sh hao_tongliang
    if [ `cat /mnt/ssd/flsm/lock` != "hao_tongliang" ]; then
        echo "Server lock acquiring failed: held by" `cat /mnt/ssd/flsm/lock`
        exit 1
    fi
fi

export ASAN_OPTIONS=use_odr_indicator=1

make test_mt TEST_LOAD='1' TEST_ARGS='31 244 244 10 8 8' 2>&1 | tee log/1thread/near256/244K-sync-256.log
make test_mt TEST_LOAD='1' TEST_ARGS='31 248 248 10 8 8' 2>&1 | tee log/1thread/near256/248K-sync-256.log
make test_mt TEST_LOAD='1' TEST_ARGS='31 252 252 10 8 8' 2>&1 | tee log/1thread/near256/252K-sync-256.log
make test_mt TEST_LOAD='1' TEST_ARGS='31 256 256 10 8 8' 2>&1 | tee log/1thread/near256/256K-sync-256.log
make test_mt TEST_LOAD='1' TEST_ARGS='31 260 260 10 8 8' 2>&1 | tee log/1thread/near256/260K-sync-256.log
make test_mt TEST_LOAD='1' TEST_ARGS='31 264 264 10 8 8' 2>&1 | tee log/1thread/near256/264K-sync-256.log
make test_mt TEST_LOAD='1' TEST_ARGS='31 268 268 10 8 8' 2>&1 | tee log/1thread/near256/268K-sync-256.log

if [ `hostname` == erode-lom ]; then
    /mnt/ssd/flsm/unlock_server.sh hao_tongliang
fi
