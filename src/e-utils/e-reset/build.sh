#!/bin/bash

set -e

echo "Building target: e-reset"

EINCS="-I ../../e-hal/src"
EINCS="${EINCS} -I ../../e-loader/src"
EINCS="${EINCS} -I ${EPIPHANY_HOME}/tools/host/include"

ELIBS="-L ../../e-hal/Release"
ELIBS="${ELIBS} -L ${EPIPHANY_HOME}/tools/host/lib"

echo "Invoking: GCC C Compiler"
gcc ${ELIBS} ${EINCS} e-reset.c -o e-reset -le-hal
echo "Finished building target: e-reset"

