#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <err.h>
#include "epiphany-hal.h"
#include "esim-target.h"

extern e_platform_t e_platform;

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

// Read a word from SRAM of a core in a group
static int ee_read_word_esim(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr)
{
	int data;
	ssize_t size;
	uint32_t addr;

	size = sizeof(int);
	addr = (dev->core[row][col].id << 20) + from_addr;

	if (ES_OK != es_ops.mem_load(dev->esim, addr, size, (uint8_t *) &data))
	{
		warnx("ee_read_word(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_read_word(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	return data;
}

// Write a word to SRAM of a core in a group
static ssize_t ee_write_word_esim(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data)
{
	ssize_t  size;
	uint32_t addr;

	size = sizeof(int);
	addr = (dev->core[row][col].id << 20) + to_addr;

	if (ES_OK != es_ops.mem_store(dev->esim, addr, size, (uint8_t *) &data))
	{
		warnx("ee_write_word(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_write_word(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	return sizeof(int);
}

// Read a memory block from SRAM of a core in a group
static ssize_t ee_read_buf_esim(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr, void *buf, size_t size)
{
	uint32_t addr;

	addr = (dev->core[row][col].id << 20) + from_addr;

	if (ES_OK != es_ops.mem_load(dev->esim, addr, size, (uint8_t *) buf))
	{
		warnx("ee_read_buf(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_read_buf(): reading from from_addr=0x%08x, pfrom=0x%08x, size=%d\n", (uint) from_addr, (uint) pfrom, (int) size); }
	return size;
}

// Write a memory block to SRAM of a core in a group
static ssize_t ee_write_buf_esim(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, const void *buf, size_t size)
{
	uint32_t addr;

	addr = (dev->core[row][col].id << 20) + to_addr;

	if (ES_OK != es_ops.mem_store(dev->esim, addr, size, (uint8_t *) buf))
	{
		warnx("ee_write_buf(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_write_buf(): writing to to_addr=0x%08x, pto=0x%08x, size=%d\n", (uint) to_addr, (uint) pto, (int) size); }
	return size;
}

// Read a core register from a core in a group
static int ee_read_reg_esim(e_epiphany_t *dev, unsigned row, unsigned col, const off_t from_addr)
{
	uint32_t addr;
	int data;
	off_t   from_addr_adjusted;
	ssize_t size;

	from_addr_adjusted = from_addr;
	if (from_addr_adjusted < E_REG_R0)
		from_addr_adjusted = from_addr_adjusted + E_REG_R0;

	addr = (dev->core[row][col].id << 20) + from_addr_adjusted;

	size = sizeof(int);
	if (ES_OK != es_ops.mem_load(dev->esim, addr, size, (uint8_t *) &data))
	{
		warnx("ee_read_reg(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_read_reg(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }
	return data;
}

// Write to a core register of a core in a group
static ssize_t ee_write_reg_esim(e_epiphany_t *dev, unsigned row, unsigned col, off_t to_addr, int data)
{
	uint32_t addr;
	ssize_t size;

	if (to_addr < E_REG_R0)
		to_addr = to_addr + E_REG_R0;

	addr = (dev->core[row][col].id << 20) + to_addr;

	size = sizeof(int);
	if (ES_OK != es_ops.mem_store(dev->esim, addr, size, (uint8_t *) &data))
	{
		warnx("ee_write_reg(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_write_reg(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	return size;
}

// Read a word from an external memory buffer
static int ee_mread_word_esim(e_mem_t *mbuf, const off_t from_addr)
{
	int data;
	uint32_t addr;
	ssize_t size;

	/* ???: Not sure whether this is always the right address */
	addr = mbuf->ephy_base + from_addr + mbuf->page_offset;

	size = sizeof(int);
	if (ES_OK != es_ops.mem_load(mbuf->esim, addr, size, (uint8_t *) &data))
	{
		warnx("ee_mread_word(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_mread_word(): reading from from_addr=0x%08x, pfrom=0x%08x\n", (uint) from_addr, (uint) pfrom); }

	return data;
}

// Write a word to an external memory buffer
static ssize_t ee_mwrite_word_esim(e_mem_t *mbuf, off_t to_addr, int data)
{
	uint32_t addr;
	ssize_t size;

	/* ???: Not sure whether this is always the right address */
	addr = mbuf->ephy_base + to_addr;

	size = sizeof(int);
	if (ES_OK != es_ops.mem_store(mbuf->esim, addr, size, (uint8_t *) &data))
	{
		warnx("ee_mwrite_word(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_mwrite_word(): writing to to_addr=0x%08x, pto=0x%08x\n", (uint) to_addr, (uint) pto); }
	return size;
}

// Read a block from an external memory buffer
static ssize_t ee_mread_buf_esim(e_mem_t *mbuf, const off_t from_addr, void *buf, size_t size)
{
	uint32_t addr;

	/* ???: Not sure whether this is always the right address */
	addr = mbuf->ephy_base + mbuf->page_offset + from_addr;

	if (ES_OK != es_ops.mem_load(mbuf->esim, addr, size, (uint8_t *) buf))
	{
		warnx("ee_mread_buf(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_mread_buf(): reading from from_addr=0x%08x, pfrom=0x%08x, size=%d\n", (uint) from_addr, (uint) pfrom, (uint) size); }
	return size;
}

// Write a block to an external memory buffer
static ssize_t ee_mwrite_buf_esim(e_mem_t *mbuf, off_t to_addr, const void *buf, size_t size)
{
	uint32_t addr;

	/* ???: Not sure whether this is always the right address */
	addr = mbuf->ephy_base + mbuf->page_offset + to_addr;

	if (ES_OK != es_ops.mem_store(mbuf->esim, addr, size, (uint8_t *) buf))
	{
		warnx("ee_mwrite_buf(): Failed.");
		return E_ERR;
	}
	//diag(H_D2) { fprintf(diag_fd, "ee_mwrite_buf(): writing to to_addr=0x%08x, pto=0x%08x, size=%d\n", (uint) to_addr, (uint) pto, (uint) size); }
	return size;
}

// Reset the Epiphany platform
static int e_reset_system_esim(void)
{
	e_epiphany_t dev;

	// diag(H_D1) { fprintf(diag_fd, "e_reset_system(): resetting full ESYS...\n"); }

	if (E_OK != e_open(&dev, 0, 0, e_platform.rows, e_platform.cols))
	{
		warnx("e_reset_system(): e_open() failure.");
		return E_ERR;
	}
	if (E_OK != e_reset_group(&dev))
	{
		warnx("e_reset_system(): e_reset_group() failure.");
		return E_ERR;
	}

	// TODO: clear core SRAM
	// TODO: clear external ram ??

	return E_OK;
}

static int ee_populate_platform_esim(e_platform_t *dev, char *hdf)
{
#ifdef ESIM_TARGET
	es_cluster_cfg cfg;

	es_get_cluster_cfg(dev->esim, &cfg);

	memcpy(&dev->version, "PARALLELLASIM", sizeof("PARALLELLASIM"));

	dev->num_chips = 1;
	dev->chip = (e_chip_t *) calloc(1, sizeof(e_chip_t));

	/* Only one mem region supported in simulator */
	dev->num_emems = 1;
	dev->emem = (e_memseg_t *) calloc(1, sizeof(e_memseg_t));

	memcpy(dev->chip[0].version, "ESIM", sizeof("ESIM"));

	dev->chip[0].row = cfg.row_base;
	dev->chip[0].col = cfg.col_base;
	dev->emem[0].phy_base = cfg.ext_ram_base;
	dev->emem[0].ephy_base = cfg.ext_ram_base;
	dev->emem[0].size = cfg.ext_ram_size;
	dev->emem[0].type = E_RDWR;

	/* Fill in chip param table */
	chip_params_table[E_ESIM].sram_size = cfg.core_phys_mem;
	chip_params_table[E_ESIM].rows = cfg.rows;
	chip_params_table[E_ESIM].cols = cfg.cols;

	return E_OK;
#else
	return E_ERR;
#endif
}

static int ee_init_esim()
{
	if (ES_OK != es_ops.client_connect(&e_platform.esim, NULL)) {
		warnx("e_init(): Cannot connect to ESIM");
		return E_ERR;
	}
	return E_OK;
}

static void ee_finalize_esim()
{
	es_ops.client_disconnect(e_platform.esim, true);
}

/* ESIM target ops */
const struct e_target_ops esim_target_ops = {
	.ee_read_word = ee_read_word_esim,
	.ee_write_word = ee_write_word_esim,
	.ee_read_buf = ee_read_buf_esim,
	.ee_write_buf = ee_write_buf_esim,
	.ee_read_reg = ee_read_reg_esim,
	.ee_write_reg = ee_write_reg_esim,
	.ee_mread_word = ee_mread_word_esim,
	.ee_mwrite_word = ee_mwrite_word_esim,
	.ee_mread_buf = ee_mread_buf_esim,
	.ee_mwrite_buf = ee_mwrite_buf_esim,
	.e_reset_system = e_reset_system_esim,
	.populate_platform = ee_populate_platform_esim,
	.init = ee_init_esim,
	.finalize = ee_finalize_esim,
};
