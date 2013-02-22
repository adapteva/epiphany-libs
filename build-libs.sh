#!/bin/bash

set -e

ESDK=${EPIPHANY_HOME}
BSP='zed_E64G4_808_512mb'
ARCH='armv7l'

#if [ ! -d "${ESDK}/tools/host/lib" ]; then
#	mkdir -p ${ESDK}/tools/host/${ARCH}/lib
#	mkdir -p ${ESDK}/tools/host/${ARCH}/include
#	mkdir -p ${ESDK}/tools/host/${ARCH}/bin
#fi

#if [ ! -d "${ESDK}/bsps" ]; then
#	mkdir -p ${ESDK}/bsps
#fi

#if [ ! -d "${ESDK}/tools/e-gnu/epiphany-elf/lib" ]; then
#	echo "Please install the Epiphany GNU tools suite first at ${ESDK}/tools/e-gnu!"
#	exit
#fi


## Install the current BSP
#cp -Rd bsps/${BSP} ${ESDK}/bsps/
#ln -sTf ${BSP} ${ESDK}/bsps/bsp

# Build and install the XML parser library
cd src/e-xml/Release
make clean
make all
#cp -f libe-xml.so ${ESDK}/tools/host/${ARCH}/lib
cd ../../../

# Build and install the Epiphnay HAL library
cd src/e-hal/Release
make clean
make all
#cp -f libe-hal.so ${ESDK}/bsps/${BSP}
#ln -sTf ../../../../bsps/bsp/libe-hal.so ${ESDK}/tools/host/${ARCH}/lib/libe-hal.so
#cp -f ../src/epiphany-hal.h ${ESDK}/tools/host/${ARCH}/include
#cp -f ../src/epiphany-hal-defs.h ${ESDK}/tools/host/${ARCH}/include
#cp -f ../src/epiphany-hal-data.h ${ESDK}/tools/host/${ARCH}/include
#cp -f ../src/epiphany-hal-api.h ${ESDK}/tools/host/${ARCH}/include
#ln -sTf epiphany-hal.h ${ESDK}/tools/host/${ARCH}/include/e-hal.h
#ln -sTf epiphany-hal.h ${ESDK}/tools/host/${ARCH}/include/e_hal.h
cd ../../../

# Build and install the Epiphnay Loader library
cd src/e-loader/Release
make clean
make all
#cp -f libe-loader.so ${ESDK}/tools/host/${ARCH}/lib
#cp -f libe-loader.a ${ESDK}/tools/host/${ARCH}/lib
#cp -f ../src/e-loader.h ${ESDK}/tools/host/${ARCH}/include
#ln -sTf e-loader.h ${ESDK}/tools/host/${ARCH}/include/e_loader.h
cd ../../../

# Build and install the Epiphnay GDB RSP Server
cd src/e-server/Release
make clean
make all
#cp -f e-server ${ESDK}/tools/host/${ARCH}/bin
cd ../../../

# Install the Epiphnay GNU Tools wrappers
cd src/e-utils
#cp -f e-objcopy ${ESDK}/tools/host/${ARCH}/bin
cd ../../

# build and install the Epiphnay Runtime Library
cd src/e-lib/Release
make clean
make all
#cp libe-lib.a ${ESDK}/tools/e-gnu/epiphany-elf/lib
#cp ../include/*.h ${ESDK}/tools/e-gnu/epiphany-elf/sys-include/
cd ../../../

