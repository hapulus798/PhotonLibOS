#!/bin/bash

sudo perf record -g --call-graph=dwarf -o ./perfres/4k-128.dwarf.perf.data ./build/output/test-spdknvme --gtest_filter="SPDKNVMeTest.performance2"

# report
# sudo perf report -i ./perfres/4k-128.perf.data