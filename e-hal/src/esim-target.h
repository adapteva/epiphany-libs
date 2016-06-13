#ifndef __EHAL_ESIM_H
#define __EHAL_ESIM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef ESIM_TARGET
#include <esim.h>
#endif

#include "epiphany-hal-data.h"

#define ES_OK 0
typedef struct es_cluster_cfg_ es_cluster_cfg;
typedef struct es_state_ es_state;
struct esim_ops {
	int (*client_connect) (es_state **, const char *);
	void (*client_disconnect)(es_state *, bool);
	volatile void * (*client_get_raw_pointer) (es_state *, uint64_t, uint64_t);

	int (*mem_store)(es_state *, uint64_t, uint64_t, uint8_t *);
	int (*mem_load)(es_state *, uint64_t, uint64_t, uint8_t *);
	int (*initialized)(const es_state *);

	void (*get_cluster_cfg)(const es_state *, es_cluster_cfg *);
};

extern const struct esim_ops es_ops;
extern bool ee_esim_target_p();

#endif
