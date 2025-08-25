#!/bin/bash

# WARNING !!! must check this disk is not mount and nobody using

# sudo -v
# sudo ./build/examples-output/io-perf --disk-path="/dev/nvme5n1" --disk-size=1073741824 --io_uring=true --io_size=4096 --io_depth=64

# bs_values=(512b 1k 4k 16k 32k 64k 128k 512k)
bs_values=(512 1024 4096 16384 32768 65536 131072 524288)
iodepth_values=(1 16 32 64 128)

for iodepth in "${iodepth_values[@]}"; do
    for bs in "${bs_values[@]}"; do
        echo "Running test with iodepth=$iodepth, bs=$bs"
        { sudo ./build/examples-output/io-perf --disk-path="/dev/nvme5n1" --disk-size=1099511627776 --io_uring=true --io_size=$bs --io_depth=$iodepth --iswrite=true | tee ./tmp/io-perf-uring-write/${iodepth}_${bs}.txt | head -n 15 > /dev/null; } 3>&1
    done
done


for iodepth in "${iodepth_values[@]}"; do
    for bs in "${bs_values[@]}"; do
        echo "Running test with iodepth=$iodepth, bs=$bs"
        { sudo ./build/examples-output/io-perf --disk-path="/dev/nvme5n1" --disk-size=1099511627776 --io_uring=false --io_size=$bs --io_depth=$iodepth --iswrite=true | tee ./tmp/io-perf-libaio-write/${iodepth}_${bs}.txt | head -n 15 > /dev/null; } 3>&1
    done
done