#!/bin/bash

set -e

ELIBS="${EPIPHANY_HOME}/tools/host/lib"
EHDF="${EPIPHANY_HOME}/bsps/current/platform.hdf"
EXML="${EPIPHANY_HOME}/bsps/current/platform.xml"

LD_LIBRARY_PATH=${ELIBS} EPIPHANY_HDF=${EHDF} ${EPIPHANY_HOME}/tools/host/bin/e-server.e -hdf ${EXML} $@

