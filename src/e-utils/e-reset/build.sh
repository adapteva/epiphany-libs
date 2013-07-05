#!/bin/bash

set -e

EINCS="${EPIPHANY_HOME}/tools/host/include"
ELIBS="${EPIPHANY_HOME}/tools/host/lib"

gcc -L ${ELIBS} -I ${EINCS} e-reset.c -o e-reset.e -le-hal

