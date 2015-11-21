/*
  File: memman.c

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2014 Adapteva, Inc.
  See AUTHORS for list of contributors
  Support e-mail: <support@adapteva.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License (LGPL)
  as published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  and the GNU Lesser General Public License along with this program,
  see the files COPYING and COPYING.LESSER.	 If not, see
  <http://www.gnu.org/licenses/>.
*/

#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/** Zero if mem manager is uninitialized, non-zero otherwise */ 
static int is_initialized = 0;

/** The first valid address of managed memory */
static void *mem_start = 0;

/** The last valid address of managed memory */
static void *mem_end = 0;

/**
 * The mem_control_block holds metadata for
 * each allocated chunk of memory
 */
typedef struct mem_control_block
{
	unsigned int is_inuse;
	unsigned int size;
} mem_ctl_blk_t;

/** Coalesce adjacent memory blocks. Note this does forward
 * coalescing only  */
static void coalesce(mem_ctl_blk_t *mcb);

int memman_init(void *start, size_t size)
{
	if ( (start == NULL) || (size == 0) ) {
		return -1;
	}
	
	mem_start = start;
	mem_end = (void*)(((char*)start) + size);

	memset(start, 0, size);

	is_initialized = 1;

	return 0;
}

void *memman_alloc(size_t size)
{
	void          *current_location      = 0;
	mem_ctl_blk_t *current_location_mcb  = 0;
	void          *mem_location          = 0;

	/* Account for the size of the memory control block */
	size += sizeof(mem_ctl_blk_t);

	current_location = mem_start;

	if ( !is_initialized ) {
		/* Ooops, foget to call memman_init() ? */
		return NULL;
	}

	while ( current_location < mem_end ) {
		current_location_mcb = (mem_ctl_blk_t*)current_location;

		if ( !current_location_mcb->is_inuse ) {
			if ( (current_location_mcb->size == 0) ||
				 current_location_mcb->size >= size ) {
				/* Found a free location */

				current_location_mcb->is_inuse = 1;
				current_location_mcb->size = size;
				mem_location = current_location;
		
				break;
			}
		}

		/* Advance to the next chunk location */
		current_location = current_location + current_location_mcb->size;
	}
	
	if ( mem_location ) {
		/* Advance the location past the memory control block */
		mem_location = ((char*)mem_location) + sizeof(mem_ctl_blk_t);
	}

	return mem_location;
}


/**
 * Coalesce adjacent memory blocks. Note this does forward
 * coalescing only
 */
static void coalesce(mem_ctl_blk_t *mcb)
{
	mem_ctl_blk_t *pmcb = mcb;
	unsigned int   free_space = 0;

	assert(pmcb);

	while ( !pmcb->is_inuse && (pmcb->size != 0) ) {
		free_space += pmcb->size;

		pmcb = (mem_ctl_blk_t*)(((char*)pmcb) + pmcb->size);
	}

	mcb->size = free_space;
}

void memman_free(void *ptr)
{
	mem_ctl_blk_t *mcb = 0;  

	/* Backup from the given pointer to find the 
	 * mem_control_block */ 
	mcb = (mem_ctl_blk_t*)(((char*)ptr) - sizeof(mem_ctl_blk_t));

	/* Mark the block as being available */ 
	mcb->is_inuse = 0;

	/* Coalesce unused regions adjacent to this chunk */
	coalesce(mcb);

	return;   	
}
