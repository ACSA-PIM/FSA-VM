#!/bin/zsh

source ../test-env.sh
mkdir output-radix
$ZSIM_BIN_PATH/zsim ./pim_radix.cfg cmd.cfg ./output-radix
