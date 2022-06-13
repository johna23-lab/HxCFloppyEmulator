#!/bin/bash
#
# Cross compiler and Linux generation scripts
# (c)2014-2019 Jean-François DEL NERO
#
# DE10-Nano target kernel compilation
#

source ${TARGET_CONFIG}/config.sh || exit 1

cp ${TARGET_CONFIG}/patches/ssd1307fb.c ./drivers/video/fbdev/ || exit 1
