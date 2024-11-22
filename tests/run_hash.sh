#!/bin/zsh

source ../test-env.sh
mkdir output-hash
$ZSIM_BIN_PATH/zsim ./pim_hash.cfg cmd.cfg ./output-hash
