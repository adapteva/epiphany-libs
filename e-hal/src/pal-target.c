#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <err.h>
#include "epiphany-hal.h"

#include <pal/pal.h>

extern e_platform_t e_platform;

extern void *_p_map_raw(p_dev_t dev, unsigned long address, unsigned long size);

struct pal_member {
	p_mem_t mem;
	p_prog_t prog;
};

struct pal_data {
	p_dev_t *dev;
	p_team_t team;
	struct pal_member *member;
};

static unsigned pal_to_rank(e_epiphany_t *dev, unsigned row, unsigned col)
{
	return row * e_platform.cols + col;
}

/* Callback functions */

// Allocate a buffer in external memory
static int pal_alloc(e_mem_t *mbuf)
{
	p_dev_t *dev = &e_platform.priv;

	if (!*dev)
		return E_ERR;

	p_mem_t *mem = malloc(sizeof(*mem));
	if (!mem)
		return E_ERR;

	*mem = p_map(*dev, mbuf->ephy_base, mbuf->emap_size);
	if (p_mem_error(mem)) {
		free(mem);
		return E_ERR;
	}

	mbuf->priv = mem;
	return E_OK;
}

// Free a memory buffer in external memory
static int pal_free(e_mem_t *mbuf)
{
	p_mem_t *mem = mbuf->priv;

	if (!mem)
		return E_OK;

	/* p_unmap ... */

	free(mem);

	mbuf->priv = NULL;

	return E_OK;
}

// Read a memory block from SRAM of a core in a group
static ssize_t pal_read_buf(e_epiphany_t *dev, unsigned row, unsigned col,
							const off_t from_addr, void *buf, size_t size)
{
	struct pal_data *pal = dev->priv;
	unsigned rank, offset;
	ssize_t bytes;

	rank = pal_to_rank(dev, row, col);
	offset = from_addr & 0xfffff;

	bytes = p_read(&pal->member[rank].mem, buf, offset, size, 0);
	if (bytes != size)
		return -E_ERR;

	return bytes;
}

// Write a memory block to SRAM of a core in a group
static ssize_t pal_write_buf(e_epiphany_t *dev, unsigned row, unsigned col,
							 off_t to_addr, const void *buf, size_t size)
{
	struct pal_data *pal = dev->priv;
	unsigned rank, offset;
	ssize_t bytes;

	rank = pal_to_rank(dev, row, col);
	offset = to_addr & 0xfffff;

	bytes = p_write(&pal->member[rank].mem, buf, offset, size, 0);
	if (bytes != size)
		return -E_ERR;

	return bytes;
}


// Read a word from SRAM of a core in a group
static int pal_read_word(e_epiphany_t *dev, unsigned row, unsigned col,
						 const off_t from_addr)
{
	int data;
	ssize_t bytes;
	off_t offset;

	offset = from_addr & 0xfffff;

	bytes = pal_read_buf(dev, row, col, offset, &data, sizeof(data));
	if (bytes != sizeof(data))
		return -E_ERR;

	return data;
}

// Write a word to SRAM of a core in a group
static ssize_t pal_write_word(e_epiphany_t *dev, unsigned row, unsigned col,
							  off_t to_addr, int data)
{
	to_addr &= 0xfffff;

	return pal_write_buf(dev, row, col, to_addr, &data, sizeof(data));
}

// Read a core register from a core in a group
static int pal_read_reg(e_epiphany_t *dev, unsigned row, unsigned col,
						const off_t from_addr)
{
	ssize_t bytes;
	int data;
	off_t offset;

	offset = from_addr & 0xfffff;
	offset |= 0xf0000;

	bytes = pal_read_buf(dev, row, col, offset, &data, sizeof(data));
	if (bytes != sizeof(data))
		return -E_ERR;

	return data;
}

// Write to a core register of a core in a group
static ssize_t pal_write_reg(e_epiphany_t *dev, unsigned row, unsigned col,
							 off_t to_addr, int data)
{
	to_addr &= 0xfffff;
	to_addr |= 0xf0000;

	return pal_write_buf(dev, row, col, to_addr, &data, sizeof(data));
}

// Read a block from an external memory buffer
static ssize_t pal_mread_buf(e_mem_t *mbuf, const off_t from_addr, void *buf,
							 size_t size)
{
	p_mem_t *mem = mbuf->priv;
	uint32_t offset;

	/* ???: Not sure whether this is always the right address */
	offset = mbuf->page_offset + from_addr;

	return p_read(mem, buf, offset, size, 0);
}

// Write a block to an external memory buffer
static ssize_t pal_mwrite_buf(e_mem_t *mbuf, off_t to_addr, const void *buf,
							  size_t size)
{
	p_mem_t *mem = mbuf->priv;
	uint32_t offset;

	/* ???: Not sure whether this is always the right address */
	offset = mbuf->page_offset + to_addr;

	return p_write(mem, buf, offset, size, 0);
}

// Read a word from an external memory buffer
static int pal_mread_word(e_mem_t *mbuf, const off_t from_addr)
{
	int word, err;
	err = pal_mread_buf(mbuf, from_addr, &word, sizeof(word));

	return err < 0 ? err : word;
}

// Write a word to an external memory buffer
static ssize_t pal_mwrite_word(e_mem_t *mbuf, off_t to_addr, int data)
{
	int word, err;
	err = pal_mwrite_buf(mbuf, to_addr, &word, sizeof(word));

	return err < 0 ? err : word;
}


// Reset the Epiphany platform
static int pal_reset_system(void)
{
	return E_OK;
}

static int pal_populate_platform(e_platform_t *dev, char *hdf)
{
	p_dev_t *pal_dev = &e_platform.priv;
	unsigned row_base, col_base, cols, rows, sram_size;

	/* TODO: Some hardcoded values. */

	memcpy(&dev->version, "PAL", sizeof("PAL"));

	dev->num_chips = 1;
	dev->chip = (e_chip_t *) calloc(1, sizeof(e_chip_t));

	dev->num_emems = 1;
	dev->emem = (e_memseg_t *) calloc(1, sizeof(e_memseg_t));

	memcpy(dev->chip[0].version, "PAL", sizeof("PAL"));

	row_base = p_query(*pal_dev, P_PROP_ROWBASE);
	col_base = p_query(*pal_dev, P_PROP_COLBASE);
	rows = p_query(*pal_dev, P_PROP_ROWS);
	cols = p_query(*pal_dev, P_PROP_COLS);
	sram_size = p_query(*pal_dev, P_PROP_MEMSIZE);

	dev->chip[0].row = row_base;
	dev->chip[0].col = col_base;

	/* Fill in chip param table */
	e_chip_params_table[E_ESIM].sram_size = sram_size;
	e_chip_params_table[E_ESIM].rows = rows;
	e_chip_params_table[E_ESIM].cols = cols;

	/* TODO: Parameters not exposed in PAL */
	dev->emem[0].phy_base = 0x8e000000;
	dev->emem[0].ephy_base = 0x8e000000;
	dev->emem[0].size = 32 * 1024 * 1024;
	dev->emem[0].type = E_RDWR;

	return E_OK;
}

static int pal_init()
{
	p_dev_t pal_dev;

	e_platform.priv = NULL;

	pal_dev = p_init(P_DEV_EPIPHANY, 0);
	if (p_error(pal_dev))
		return E_ERR;

	e_platform.priv = pal_dev;
	return E_OK;
}

static void pal_finalize()
{
	p_dev_t pal_dev = e_platform.priv;

	if (!pal_dev)
		return;

	p_finalize(pal_dev);

	e_platform.priv = NULL;
}

static int init_glob_pal_team()
{
}

static int pal_open(e_epiphany_t *dev, unsigned row, unsigned col,
					unsigned rows, unsigned cols)
{
	struct pal_data *pal;
	unsigned i, j, rank, count, start;

	if (!e_platform.priv)
		return E_ERR;

	pal = calloc(sizeof(*pal), 1);
	if (!pal)
		return E_ERR;

	pal->dev = &e_platform.priv;

	count = rows * e_platform.cols - (e_platform.cols - cols);
	start = row * e_platform.cols + col;

	pal->member = calloc(count, sizeof(struct pal_member));
	if (!pal->member)
		return E_ERR;

	pal->team = p_open(*pal->dev, start, count);
	if (p_error(pal->team))
		goto err;

	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			uint64_t base =
				((e_platform.row + row + i) * 64 + (e_platform.col + col + j)) << 20;
			pal->member[e_platform.cols * i + j].mem =
				p_map(*pal->dev, base, 0x100000);
		}
	}

	dev->priv = pal;
	return E_OK;

err:
	free(pal);
	return E_ERR;
}

static int pal_close(e_epiphany_t *dev)
{
	struct pal_data *pal = dev->priv;

	if (!pal)
		return E_ERR;

	if (pal->team) {
		p_wait(pal->team);
		p_close(pal->team);
	}

	/* Unmap members */

	if (pal->member) {
		free(pal->member);
		pal->member = NULL;
	}

	free(pal);
	dev->priv = NULL;
	return E_OK;
}

static int pal_load_group(const char *executable, e_epiphany_t *dev,
						  unsigned row, unsigned col,
						  unsigned rows, unsigned cols)
{
	struct pal_data *pal = dev->priv;
	p_prog_t prog;
	unsigned i, j, rank;

	if (!pal)
		return E_ERR;

	prog = p_load(*pal->dev, executable, 0);
	if (p_error(prog))
		return E_ERR;

	for (i = row; i < rows; i++) {
		for (j = col; j < cols; j++) {
			rank = pal_to_rank(dev, i, j);
			pal->member[rank].prog = prog;
		}
	}

	return E_OK;
}

static  int pal_start_group(e_epiphany_t *dev, unsigned row, unsigned col,
							unsigned rows, unsigned cols)
{
	struct pal_data *pal = dev->priv;
	unsigned i, j, rank;

	for (i = row; i < row + rows; i++) {
		for (j = col; j < col + cols; j++) {
			rank = pal_to_rank(dev, i, j);

			if (p_run(pal->member[rank].prog, "main", pal->team,
					  rank, 1, 0, NULL, P_RUN_NONBLOCK))
				return E_ERR;
		}
	}

	return E_OK;
}

static void *pal_get_raw_pointer(unsigned long addr, unsigned long size)
{
	p_dev_t *dev = &e_platform.priv;

	if (!dev)
		return NULL;

	return _p_map_raw(*dev, addr, size);
}

/* PAL target ops */
const struct e_target_ops pal_target_ops = {
	.ee_read_word      = pal_read_word,
	.ee_write_word     = pal_write_word,
	.ee_read_buf       = pal_read_buf,
	.ee_write_buf      = pal_write_buf,
	.ee_read_reg       = pal_read_reg,
	.ee_write_reg      = pal_write_reg,
	.ee_mread_word     = pal_mread_word,
	.ee_mwrite_word    = pal_mwrite_word,
	.ee_mread_buf      = pal_mread_buf,
	.ee_mwrite_buf     = pal_mwrite_buf,
	.e_reset_system    = pal_reset_system,
	.populate_platform = pal_populate_platform,
	.init              = pal_init,
	.finalize          = pal_finalize,
	.open              = pal_open,
	.close             = pal_close,
	.load_group        = pal_load_group,
	.start_group       = pal_start_group,
	.get_raw_pointer   = pal_get_raw_pointer,
	.alloc             = pal_alloc,
	.shm_alloc         = pal_alloc,
	.free              = pal_free,
};
