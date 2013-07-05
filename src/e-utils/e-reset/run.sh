#!/bin/bash

set -e

EBINS="${EPIPHANY_HOME}/tools/host/bin"
ELIBS="${EPIPHANY_HOME}/tools/host/lib"
EHDF="${EPIPHANY_HOME}/bsps/zed_E16G3_512mb/zed_E16G3_512mb.hdf"
#EHDF="${EPIPHANY_HOME}/bsps/zed_E64G4_512mb/zed_E64G4_512mb.hdf"

sudo -E LD_LIBRARY_PATH=${ELIBS} EPIPHANY_HDF=${EHDF} ./e-reset.e $@
