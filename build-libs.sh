#!/bin/bash

set -e

ESDK=${EPIPHANY_HOME}
BSP='zed_E64G4_808_512mb'
ARCH='armv7l'

#if [ ! -d "${ESDK}/tools/e-gnu/epiphany-elf/lib" ]; then
#	echo "Please install the Epiphany GNU tools suite first at ${ESDK}/tools/e-gnu!"
#	exit
#fi


# Build and install the XML parser library
cd src/e-xml/Release
make clean
make all
cd ../../../

# Build and install the Epiphnay HAL library
cd src/e-hal/Release
make clean
make all
cp -f libe-hal.so ../../../bsps/${BSP}
cd ../../../

# Build and install the Epiphnay Loader library
cd src/e-loader/Release
make clean
make all
cd ../../../

# Build and install the Epiphnay GDB RSP Server
cd src/e-server/Release
make clean
make all
cd ../../../

# Install the Epiphnay GNU Tools wrappers
cd src/e-utils
cd ../../

# build and install the Epiphnay Runtime Library
cd src/e-lib/Release
make clean
make all
cd ../../../

