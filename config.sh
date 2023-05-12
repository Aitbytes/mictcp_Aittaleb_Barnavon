#!/bin/bash

version=$1
lossacceptability=$2
echo $version

sed -i "s/version=.*;/version=$version;/g" "./src/mictcp.c"
sed -i "s/#define\ LOSS_ACCEPTABILITY.*/#define\ LOSS_ACCEPTABILITY\ $lossacceptability/g" "./src/mictcp.c"


make
