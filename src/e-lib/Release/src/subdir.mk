################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/e_coreid_config.c \
../src/e_coreid_coords_from_coreid.c \
../src/e_coreid_from_coords.c \
../src/e_coreid_get_coreid.c \
../src/e_coreid_get_global_address.c \
../src/e_coreid_is_oncore.c \
../src/e_coreid_neighbor_id.c \
../src/e_ctimer_sleep.c \
../src/e_ctimer_stop.c \
../src/e_dma_busy.c \
../src/e_dma_copy.c \
../src/e_dma_set_dma_desc.c \
../src/e_dma_start.c \
../src/e_dma_wait.c \
../src/e_irq_attach.c \
../src/e_irq_clear.c \
../src/e_irq_global_mask.c \
../src/e_irq_mask.c \
../src/e_irq_set.c \
../src/e_mem_read.c \
../src/e_mem_write.c \
../src/e_mutex_barrier.c \
../src/e_mutex_barrier_init.c \
../src/e_mutex_init.c \
../src/e_mutex_lock.c \
../src/e_mutex_trylock.c \
../src/e_mutex_unlock.c \
../src/e_reg_read.c \
../src/e_reg_set_flag.c \
../src/e_reg_write.c 

S_SRCS += \
../src/e_ctimer_get.s \
../src/e_ctimer_set.s \
../src/e_ctimer_start.s 

OBJS += \
./src/e_coreid_config.o \
./src/e_coreid_coords_from_coreid.o \
./src/e_coreid_from_coords.o \
./src/e_coreid_get_coreid.o \
./src/e_coreid_get_global_address.o \
./src/e_coreid_is_oncore.o \
./src/e_coreid_neighbor_id.o \
./src/e_ctimer_get.o \
./src/e_ctimer_set.o \
./src/e_ctimer_sleep.o \
./src/e_ctimer_start.o \
./src/e_ctimer_stop.o \
./src/e_dma_busy.o \
./src/e_dma_copy.o \
./src/e_dma_set_dma_desc.o \
./src/e_dma_start.o \
./src/e_dma_wait.o \
./src/e_irq_attach.o \
./src/e_irq_clear.o \
./src/e_irq_global_mask.o \
./src/e_irq_mask.o \
./src/e_irq_set.o \
./src/e_mem_read.o \
./src/e_mem_write.o \
./src/e_mutex_barrier.o \
./src/e_mutex_barrier_init.o \
./src/e_mutex_init.o \
./src/e_mutex_lock.o \
./src/e_mutex_trylock.o \
./src/e_mutex_unlock.o \
./src/e_reg_read.o \
./src/e_reg_set_flag.o \
./src/e_reg_write.o 

C_DEPS += \
./src/e_coreid_config.d \
./src/e_coreid_coords_from_coreid.d \
./src/e_coreid_from_coords.d \
./src/e_coreid_get_coreid.d \
./src/e_coreid_get_global_address.d \
./src/e_coreid_is_oncore.d \
./src/e_coreid_neighbor_id.d \
./src/e_ctimer_sleep.d \
./src/e_ctimer_stop.d \
./src/e_dma_busy.d \
./src/e_dma_copy.d \
./src/e_dma_set_dma_desc.d \
./src/e_dma_start.d \
./src/e_dma_wait.d \
./src/e_irq_attach.d \
./src/e_irq_clear.d \
./src/e_irq_global_mask.d \
./src/e_irq_mask.d \
./src/e_irq_set.d \
./src/e_mem_read.d \
./src/e_mem_write.d \
./src/e_mutex_barrier.d \
./src/e_mutex_barrier_init.d \
./src/e_mutex_init.d \
./src/e_mutex_lock.d \
./src/e_mutex_trylock.d \
./src/e_mutex_unlock.d \
./src/e_reg_read.d \
./src/e_reg_set_flag.d \
./src/e_reg_write.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Epiphany compiler'
	e-gcc -I../include -O3 -falign-loops=8 -Wall -c -fmessage-length=0 -ffp-contract=fast -mlong-calls -mfp-mode=round-nearest -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

src/%.o: ../src/%.s
	@echo 'Building file: $<'
	@echo 'Invoking: Epiphany assembler'
	e-as  -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


