#!/bin/bash

set -e

rm -rf tmp

for i in $(ls */*/*/*.log); do
    mkdir -p "../log/$(dirname $(dirname $i))"
    truncate -s0 "../log/$(dirname $(dirname $i))/$(basename $i)"
done

for i in $(ls */*/*/*.log); do
    sed 's/^\([^[:space:]]*\)\(.*iops\)$/'$(basename $(dirname $i))'\2/' $i >> "../log/$(dirname $(dirname $i))/$(basename $i)"
done

rm -rf tmp
