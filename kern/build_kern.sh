#!/bin/bash

# Sets KERN_PATH to the corresponding path
if [ $# -eq 0 ]; then
    KERN_PATH=~/cs3231/asst2-src/kern
# If path is not supplied include --ostree=path/to/root
else
    KERN_PATH=$1
fi

current_path=`pwd`
cd $KERN_PATH/conf
echo "In directory: $KERN_PATH/conf"
echo "----------- Configuring Kernal NOW -----------"
./config ASST2

cd ../compile/ASST2
echo "In directory: $KERN_PATH/compile/ASST2"
echo "----------- Making Kernal NOW  -----------"
bmake depend
bmake
bmake install
echo "----------- Finished Making Kernel -----------"

cd $current_path

