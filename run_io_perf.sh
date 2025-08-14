#!/bin/bash

# WARNING !!! must check this disk is not mount and nobody using

sudo -v
# sudo ./build/examples-output/io-perf --disk-path="/dev/nvme5n1" --disk-size=1073741824 --io_uring=true