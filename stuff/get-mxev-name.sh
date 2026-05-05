#!/bin/bash
DEVNAME=$1
SYSFS_PATH="/sys/class/input/$DEVNAME/device"
NAME=$(cat "$SYSFS_PATH/name" 2>/dev/null | tr ' ' '_')
echo "${NAME}"
