#!/bin/bash

sudo bash run_test.sh

sudo /home/hongjingxuan.hjx/spdk/spdk_v21.04.x_test/scripts/setup.sh reset

sudo bash run_io_perf.sh