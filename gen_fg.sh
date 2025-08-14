#!/bin/bash

foldstack_tool=/home/hongjingxuan.hjx/FlameGraph/stackcollapse-perf.pl
flamegraph_tool=/home/hongjingxuan.hjx/FlameGraph/flamegraph.pl

perf script -i perfres/4k-128.dwarf.perf.data > perfres_tmp/out.perf
$foldstack_tool perfres_tmp/out.perf > perfres_tmp/out.folded
$flamegraph_tool perfres_tmp/out.folded > perfres_flame/4k-128.dwarf.svg