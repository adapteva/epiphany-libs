BUILD_ROOT  	= ../..

## This library is always compiled for the epiphany cores
CROSS_COMPILE=epiphany-elf-

LIBNAME			= e-lib

EXTRACFLAGS  	= -falign-loops=8 -fmessage-length=0 -ffp-contract=fast \
	 -mlong-calls -mfp-mode=round-nearest

# List of include search paths
INCLUDES    = \
	-I./include								\
	-I../e-utils/e-trace/include			\
	-I$(PARALLELLA_LINUX_HOME)/include/uapi

C_SRCS      = \
	e_coreid_config.c 					\
	e_coreid_coords_from_coreid.c 		\
	e_coreid_from_coords.c 				\
	e_coreid_get_coreid.c 				\
	e_coreid_get_global_address.c 		\
	e_coreid_is_oncore.c 				\
	e_coreid_neighbor_id.c 				\
	e_ctimer_stop.c 					\
	e_ctimer_wait.c 					\
	e_dma_busy.c 						\
	e_dma_copy.c 						\
	e_dma_set_desc.c					\
	e_dma_start.c 						\
	e_dma_wait.c 						\
	e_irq_attach.c 						\
	e_irq_clear.c 						\
	e_irq_global_mask.c 				\
	e_irq_mask.c 						\
	e_irq_set.c 						\
	e_mem_read.c 						\
	e_mem_write.c 						\
	e_mutex_barrier.c 					\
	e_mutex_barrier_init.c 				\
	e_mutex_init.c 						\
	e_mutex_lock.c 						\
	e_mutex_trylock.c 					\
	e_mutex_unlock.c 					\
	e_reg_read.c 						\
	e_reg_write.c 						\
	e_shm.c								\
	e_trace.c

S_SRCS = \
	e_ctimer_get.s \
	e_ctimer_set.s \
	e_ctimer_start.s 

include $(BUILD_ROOT)/build_tools/make_lib.rules

