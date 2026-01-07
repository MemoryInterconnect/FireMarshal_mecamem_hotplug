#!/bin/bash


pushd boards/default/distros/br/buildroot

patch -p1 < ../../../../../fix_br_sudo_mknod.patch

popd
