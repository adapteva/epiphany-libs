################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../src/GdbServer.cpp \
../src/MemRange.cpp \
../src/MpHash.cpp \
../src/RspConnection.cpp \
../src/RspPacket.cpp \
../src/Utils.cpp \
../src/main.cpp \
../src/ServerInfo.cpp \
../src/TargetControl.cpp \
../src/TargetControlHardware.cpp 

OBJS += \
./src/GdbServer.o \
./src/MemRange.o \
./src/MpHash.o \
./src/RspConnection.o \
./src/RspPacket.o \
./src/Utils.o \
./src/main.o \
./src/ServerInfo.o \
./src/TargetControl.o \
./src/TargetControlHardware.o 

CPP_DEPS += \
./src/GdbServer.d \
./src/MemRange.d \
./src/MpHash.d \
./src/RspConnection.d \
./src/RspPacket.d \
./src/Utils.d \
./src/main.d \
./src/ServerInfo.d \
./src/TargetControl.d \
./src/TargetControlHardware.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ $(CPPFLAGS) -I../../ -O0 -g3 -Wall -Werror -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


