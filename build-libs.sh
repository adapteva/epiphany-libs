#!/bin/bash

set -e

ESDK=${EPIPHANY_HOME}
BSP='zed_E64G4_512mb'
ARCH='armv7l'

#if [ ! -d "${ESDK}/tools/e-gnu/epiphany-elf/lib" ]; then
#	echo "Please install the Epiphany GNU tools suite first at ${ESDK}/tools/e-gnu!"
#	exit
#fi


function build-xml() {
	# Build and install the XML parser library
	echo '==============================='
	echo '============ E-XML ============'
	echo '==============================='
	cd src/e-xml/Release
	make clean
	make all
	cd ../../../
}


function build-loader() {
	# Build and install the Epiphnay Loader library
	echo '=================================='
	echo '============ E-LOADER ============'
	echo '=================================='
	cd src/e-loader/Release
	make clean
	make all
	cd ../../../
}


function build-hal() {
	# Build and install the Epiphnay HAL library
	echo '==============================='
	echo '============ E-HAL ============'
	echo '==============================='
	cd src/e-hal/Release
	make clean
	make all
	cp -f libe-hal.so ../../../bsps/${BSP}
	cd ../../../
}


function build-server() {
	# Build and install the Epiphnay GDB RSP Server
	echo '=================================='
	echo '============ E-SERVER ============'
	echo '=================================='
	cd src/e-server/Release
	make clean
	make all
	cd ../../../
}


function build-utils() {
	# Install the Epiphnay GNU Tools wrappers
	echo '================================='
	echo '============ E-UTILS ============'
	echo '================================='
	cd src/e-utils
	cd ../../
}


function build-lib() {
	# build and install the Epiphnay Runtime Library
	echo '==============================='
	echo '============ E-LIB ============'
	echo '==============================='
	cd src/e-lib/Release
	make clean
	make all
	cd ../../../
}


function usage() {
	echo "Usage: build-libs.sh [pkg-list] [-a] [-h]"
	echo "   'pkg-list' is any combination of package numbers or names"
	echo "        to be built. Items are separated by spaces and names"
	echo "        are given in lowercase (e.g, 'e-hal'). The packages"
	echo "        enumeration is as follows:"
	echo ""
	echo "        1   - E-XML"
	echo "        2   - E-LOADER"
	echo "        3   - E-HAL"
	echo "        4   - E-SERVER"
	echo "        5   - E-UTILS"
	echo "        6   - E-LIB"
	echo ""
	echo "        -a  - Build all packages"
	echo "        -h  - Print this help message"	
	echo ""
	echo "   If no target is selected, all packages will be built."
	echo ""
	echo "   Example: The following command will build e-hal, e-xml and e-lib:"
	echo ""
	echo "   $ ./build-libs.sh 3 1 e-lib"
	
	exit
}


if [ $# == 0 ]; then
	usage
fi


while [ $# > 0 ]; do
	if   [[ "$1" == "1" || "$1" == "e-xml"    ]]; then
		build-xml
		
	elif [[ "$1" == "2" || "$1" == "e-loader" ]]; then
		build-loader
		
	elif [[ "$1" == "3" || "$1" == "e-hal"    ]]; then
		build-hal
		
	elif [[ "$1" == "4" || "$1" == "e-server" ]]; then
		build-server
		
	elif [[ "$1" == "5" || "$1" == "e-utils"  ]]; then
		build-utils
		
	elif [[ "$1" == "6" || "$1" == "e-lib"    ]]; then
		build-lib
		
	elif [ "$1" == "-a" ]; then
		build-xml
		build-loader
		build-hal
		build-server
		build-utils
		build-lib
		exit

	elif [ "$1" == "-h" ]; then
		usage
	fi

	shift
done

