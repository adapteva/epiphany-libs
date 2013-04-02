################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/epiphany-hal-legacy.c \
../src/epiphany-hal.c 

OBJS += \
./src/epiphany-hal-legacy.o \
./src/epiphany-hal.o 

C_DEPS += \
./src/epiphany-hal-legacy.d \
./src/epiphany-hal.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I../../ -I../../e-hal/src -I../../e-loader/src -I/opt/adapteva/esdk/tools/host/armv7l/include -I/opt/adapteva/esdk/tools/host/include -O0 -g3 -Wall -c -fmessage-length=0 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -fPIC -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


