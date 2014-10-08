#!/bin/bash

set -e

d=$(dirname "$0")
EH=$(cd "$d/../../.." && pwd)

ELIBS="${EH}/tools/host/lib"
EHDF="${EH}/bsps/current/platform.hdf"
EXML="${EH}/bsps/current/platform.xml"

LD_LIBRARY_PATH=${ELIBS} EPIPHANY_HDF=${EHDF} ${EH}/tools/host/bin/e-server.e -hdf ${EXML} $@

