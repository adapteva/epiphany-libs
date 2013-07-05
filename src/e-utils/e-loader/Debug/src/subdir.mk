################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/e-loader.c 

OBJS += \
./src/e-loader.o 

C_DEPS += \
./src/e-loader.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I/home/ysapir/Projects/newtools/maxwell_tools/e-hal/src -I/home/ysapir/Projects/newtools/maxwell_tools/e-loader/src -I../../../esdk/epiphany-libs/src/e-hal/src -I../../../esdk/epiphany-libs/src/e-loader/src -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


