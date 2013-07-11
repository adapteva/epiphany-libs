#!/bin/bash

set -e

echo "Building target: e-hw-rev"
echo "Invoking: GCC C Compiler"
gcc e-hw-rev.c -o e-hw-rev
echo "Finished building target: e-hw-rev"

