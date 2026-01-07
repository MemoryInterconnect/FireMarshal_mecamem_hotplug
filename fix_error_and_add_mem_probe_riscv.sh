#!/bin/bash


pushd boards/default/linux

patch -p1 < ../../../fix_error_and_add_mem_probe_riscv.patch

popd
