#!/bin/bash

set -e

ESDK=${EPIPHANY_HOME}
BSPS="zed_E16G3_512mb zed_E64G4_512mb parallella_E16G3_1GB"


function build-xml() {
	# Build the XML parser library
	echo '==============================='
	echo '============ E-XML ============'
	echo '==============================='
	cd src/e-xml/${version}
	if [ "${cleanit}" = "yes" ]
	then
	    make clean
	fi
	make all
	cd ../../../
}


function build-loader() {
	# Build the Epiphany Loader library
	echo '=================================='
	echo '============ E-LOADER ============'
	echo '=================================='
	cd src/e-loader/${version}
	if [ "${cleanit}" = "yes" ]
	then
	    make clean
	fi
	make all
	cd ../../../
}


function build-hal() {
	# Build the Epiphany HAL library
	echo '==============================='
	echo '============ E-HAL ============'
	echo '==============================='
	cd src/e-hal/${version}
	if [ "${cleanit}" = "yes" ]
	then
	    make clean
	fi
	make all
	for bsp in ${BSPS}; do
		cp -f libe-hal.so ../../../bsps/${bsp}
	done
	cd ../../../
}


function build-server() {
	# Build the Epiphany GDB RSP Server
	echo '=================================='
	echo '============ E-SERVER ============'
	echo '=================================='
	cd src/e-server/${version}
	if [ "${cleanit}" = "yes" ]
	then
	    make clean
	fi
	make CPPFLAGS+="-DREVISION=${REV}" all
	cd ../../../
}


function build-utils() {
	# Install the Epiphany GNU Tools wrappers
	echo '================================='
	echo '============ E-UTILS ============'
	echo '================================='
	cd src/e-utils
	cd e-reset
	./build.sh
	cd ../
	cd e-loader/Debug
	make all
	cd ../../
	cd e-read/Debug
	make all
	cd ../../
	cd e-write/Debug
	make all
	cd ../../
	cd e-hw-rev
	./build.sh
	cd ../
	cd ../../
}


function build-lib() {
	# build the Epiphany Runtime Library
	echo '==============================='
	echo '============ E-LIB ============'
	echo '==============================='
	if [ ! -d "${ESDK}/tools/e-gnu/bin" ]; then
		echo "In order to build the E-LIB the e-gcc compiler is required. Please"
		echo "install the Epiphany GNU tools suite first at ${ESDK}/tools/e-gnu!"
		exit
	fi
	cd src/e-lib/${version}
	if [ "${cleanit}" = "yes" ]
	then
	    make clean
	fi
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
	echo "        -c  - Clean before building any packages after this"
	echo ""
	echo "   If no target is selected, all packages will be built."
	echo ""
	echo "   Example: The following command will build e-hal, e-xml and e-lib:"
	echo ""
	echo "   $ ./build-libs.sh 1 3 e-lib"
	
	exit
}

if [[ $# == 0 ]]; then
	usage
fi

cleanit="no"
version="Release"

while [[ $# > 0 ]]; do
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
		
	elif [[ "$1" == "-a" ]]; then
		build-xml
		build-loader
		build-hal
		build-server
		build-utils
		build-lib
		exit

	elif [[ "$1" == "-h" ]]; then
		usage

	elif [[  "$1" == "-c" ]]; then
		cleanit="yes"

	elif [[  "$1" == "-d" ]]; then
		version="Debug"
	fi

	shift
done


