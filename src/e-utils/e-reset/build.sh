#!/bin/bash

set -e

EINCS="../../e-hal/src"
ELIBS="../../e-hal/Release"
EINCS="-I ${EINCS} -I ${EPIPHANY_HOME}/tools/host/include"
ELIBS="-L ${ELIBS} -L ${EPIPHANY_HOME}/tools/host/lib"

gcc ${ELIBS} ${EINCS} e-reset.c -o e-reset.e -le-hal

