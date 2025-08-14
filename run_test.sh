#!/bin/bash

# bs_values=(512b 1k 4k 16k 64k 128k 512k)
bs_values=(512 1024 4096 16384 65536 131072 524288)
iodepth_values=(1 16 64 128)

for iodepth in "${iodepth_values[@]}"; do
    for bs in "${bs_values[@]}"; do
        echo "Running test with iodepth=$iodepth, bs=$bs"
        { sudo ./build/output/test-spdknvme --gtest_filter="SPDKNVMeTest.performance2" --gtest_color=no --bs=$bs --iodepth=$iodepth 2>&1 1>&3 | tee ./perftest/${iodepth}_${bs}.txt | head -n 40 > /dev/null; } 3>&1
    done
done