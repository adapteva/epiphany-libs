AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = src/e_trace_dma.c

include_HEADERS =                       \
include/e_common.h                      \
include/e_coreid.h                      \
include/e_ctimers.h                     \
include/e_dma.h                         \
include/e_ic.h                          \
include/e_lib.h                         \
include/e-lib.h                         \
include/e_mem.h                         \
include/e_mutex.h                       \
include/e_regs.h                        \
include/e_shm.h                         \
include/e_trace.h                       \
include/e_types.h

lib_LIBRARIES = libe-lib.a

libe_lib_a_SOURCES =                    \
src/e_coreid_config.c                   \
src/e_coreid_coords_from_coreid.c       \
src/e_coreid_from_coords.c              \
src/e_coreid_get_coreid.c               \
src/e_coreid_get_global_address.c       \
src/e_coreid_is_oncore.c                \
src/e_coreid_neighbor_id.c              \
src/e_ctimer_get.s                      \
src/e_ctimer_set.s                      \
src/e_ctimer_start.s                    \
src/e_ctimer_stop.c                     \
src/e_ctimer_wait.c                     \
src/e_dma_busy.c                        \
src/e_dma_copy.c                        \
src/e_dma_set_desc.c                    \
src/e_dma_start.c                       \
src/e_dma_wait.c                        \
src/e_irq_attach.c                      \
src/e_irq_clear.c                       \
src/e_irq_global_mask.c                 \
src/e_irq_mask.c                        \
src/e_irq_set.c                         \
src/e_mem_read.c                        \
src/e_mem_write.c                       \
src/e_mutex_barrier.c                   \
src/e_mutex_barrier_init.c              \
src/e_mutex_init.c                      \
src/e_mutex_lock.c                      \
src/e_mutex_trylock.c                   \
src/e_mutex_unlock.c                    \
src/e_reg_read.c                        \
src/e_reg_write.c                       \
src/e_shm.c                             \
src/e_trace.c
