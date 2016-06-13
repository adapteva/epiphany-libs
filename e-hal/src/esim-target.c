#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "esim-target.h"

const struct esim_ops es_ops = {
#ifdef ESIM_TARGET
	.client_connect = es_client_connect,
	.client_disconnect = es_client_disconnect,
	.client_get_raw_pointer = es_client_get_raw_pointer,
	.mem_store = es_mem_store,
	.mem_load = es_mem_load,
	.initialized = es_initialized,
	.get_cluster_cfg = es_get_cluster_cfg,
#else
	.client_connect = NULL,
	.client_disconnect = NULL,
	.client_get_raw_pointer = NULL,
	.mem_store = NULL,
	.mem_load = NULL,
	.initialized = NULL,
	.get_cluster_cfg = NULL,
#endif
};

bool ee_esim_target_p()
{
	static bool initialized = false;
	static bool esim = false;
	const char *p;

	if (!initialized) {
		p = getenv(EHAL_TARGET_ENV);
		esim = (p && strncmp(p, "esim", sizeof("esim")) == 0);
        initialized = true;
	}

	return esim;
}

