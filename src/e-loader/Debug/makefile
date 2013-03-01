REV := 4.13.01.04
ARCH := armv7l
PARALLELLA_HOME := ~/Projects/temp/esdk.${REV}
ESDK_HOME := /opt/adapteva/esdk

#ESDK := ${PARALLELLA_HOME}
ESDK ?= ${ESDK_HOME}
ESDK_BIN := ${ESDK}/tools/e-gnu/bin

SRC := e-loader parseSrecFile
OBJS := e-loader.o parseSrecFile.o
OBJS_STAT := e-loader.stat.o parseSrecFile.stat.o

INCS := -I ../../e-hal/src -I ${ESDK}/tools/host/${ARCH}/include
OPTFLAGS := -O0 -g3
CFLAGS := -Wall -fPIC $(OPTFLAGS) -c
LDFLAGS := -Wall -shared
CFLAGS_STAT := -Wall $(OPTFLAGS) -c
ARFLAGS := rcs

#BIN_PATH := ~/CodeSourcery/Sourcery_CodeBench_Lite_for_ARM_GNU_Linux/bin/arm-none-linux-gnueabi-
BIN_PATH ?=
CC := ${BIN_PATH}gcc
AR := ${BIN_PATH}ar

.PHONY : clean
.PHONY : all

all: libe-loader.so libe-loader.a

e-loader.o: ../src/e-loader.c ../src/e-loader.h
	@echo "--- building $@ from $<"
	$(CC) $(CFLAGS) $(INCS) -o "$@" "$<"

e-loader.stat.o: ../src/e-loader.c ../src/e-loader.h
	@echo "--- building $@ from $<"
	$(CC) $(CFLAGS_STAT) $(INCS) -o "$@" "$<"

parseSrecFile.o: ../src/parseSrecFile.c ../src/e-loader.h
	@echo "--- building $@ from $<"
	$(CC) $(CFLAGS) $(INCS) -o "$@" "$<"

parseSrecFile.stat.o: ../src/parseSrecFile.c ../src/e-loader.h
	@echo "--- building $@ from $<"
	$(CC) $(CFLAGS_STAT) $(INCS) -o "$@" "$<"

libe-loader.so: $(OBJS)
	@echo "--- building $@ from $<"
	$(CC) $(LDFLAGS) -o "$@" $(OBJS)
#	ln -sf $@ $@.1
#	ln -sf $@.1 $@.1.0

libe-loader.a: $(OBJS_STAT)
	@echo "--- building $@ from $<"
	$(AR) $(ARFLAGS) "$@" $(OBJS_STAT)

clean:
	rm -rf libe-loader.so* libe-loader.a $(OBJS) $(OBJS_STAT)

