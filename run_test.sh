#!/bin/bash

# bs_values=(512b 1k 4k 16k 32k 64k 128k 512k)
bs_values=(512 1024 4096 16384 32768 65536 131072 524288)
iodepth_values=(1 16 32 64 128)
# bs_values=(16384 32768 65536 131072)
# iodepth_values=(16 32 64 128)

for iodepth in "${iodepth_values[@]}"; do
    for bs in "${bs_values[@]}"; do
        echo "Running test with iodepth=$iodepth, bs=$bs"
        { sudo ./build/output/test-spdknvme --gtest_filter="SPDKNVMeTest.performance2" --gtest_color=no --bs=$bs --iodepth=$iodepth --iswrite=true --size=10 2>&1 1>&3 | tee ./tmp/spdk-write/${iodepth}_${bs}.txt | head -n 40 > /dev/null; } 3>&1
    done
done