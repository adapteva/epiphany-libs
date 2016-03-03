#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <err.h>
#include "epiphany-hal.h"

#include <pal/pal.h>

extern e_platform_t e_platform;

// Read a word from SRAM of a core in a group
static int pal_read_word(e_epiphany_t *dev, unsigned row, unsigned col,
						 const off_t from_addr)
{
	return E_ERR;
}

// Write a word to SRAM of a core in a group
static ssize_t pal_write_word(e_epiphany_t *dev, unsigned row, unsigned col,
							  off_t to_addr, int data)
{
	return -E_ERR;
}

// Read a memory block from SRAM of a core in a group
static ssize_t pal_read_buf(e_epiphany_t *dev, unsigned row, unsigned col,
							const off_t from_addr, void *buf, size_t size)
{
	return -E_ERR;
}

// Write a memory block to SRAM of a core in a group
static ssize_t pal_write_buf(e_epiphany_t *dev, unsigned row, unsigned col,
							 off_t to_addr, const void *buf, size_t size)
{
	return -E_ERR;
}

// Read a core register from a core in a group
static int pal_read_reg(e_epiphany_t *dev, unsigned row, unsigned col,
						const off_t from_addr)
{
	return E_ERR;
}

// Write to a core register of a core in a group
static ssize_t pal_write_reg(e_epiphany_t *dev, unsigned row, unsigned col,
							 off_t to_addr, int data)
{
	return -E_ERR;
}

// Read a word from an external memory buffer
static int pal_mread_word(e_mem_t *mbuf, const off_t from_addr)
{
	return E_ERR;
}

// Write a word to an external memory buffer
static ssize_t pal_mwrite_word(e_mem_t *mbuf, off_t to_addr, int data)
{
	return -E_ERR;
}

// Read a block from an external memory buffer
static ssize_t pal_mread_buf(e_mem_t *mbuf, const off_t from_addr, void *buf,
							 size_t size)
{
	return -E_ERR;
}

// Write a block to an external memory buffer
static ssize_t pal_mwrite_buf(e_mem_t *mbuf, off_t to_addr, const void *buf,
							  size_t size)
{
	return -E_ERR;
}

// Reset the Epiphany platform
static int pal_reset_system(void)
{
	return E_ERR;
}

static int pal_populate_platform(e_platform_t *dev, char *hdf)
{
	return E_ERR;
}

static int pal_init()
{
	return E_ERR;
}

static void pal_finalize()
{
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
};
