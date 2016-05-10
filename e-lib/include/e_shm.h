/*
  File: e_shm.h

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  See AUTHORS for list of contributors.
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

#ifndef _E_SHM_H_
#define _E_SHM_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SHM_REGIONS				   64

#include <stdint.h>
#include <sys/types.h>
#include "e_common.h"
#include "e_mem.h"

#define MAX_SHM_REGIONS				   64

/*
** Type definitions
*/
#pragma pack(push, 1)

/**
 * NOTE: The Shared Memory types must match those defined
 * in the e-hal.
 *
 * TODO: Make a common include file for e-hal/e-lib shared
 * data structures??
 */

/** Shared memory segment */
typedef struct ALIGN(8) e_shmseg {
	union {
		void	*addr;		  /* Virtual address */
		uint64_t __fill1;
	};
	char	 name[256];	  /* Region name */
	uint64_t size;		  /* Region size in bytes */
	union {
		void	*paddr;		  /* Physical Address accessible from Epiphany cores */
		uint64_t __fill2;
	};
	uint64_t offset;	  /* Offset from shm base address */
} e_shmseg_t;

typedef struct ALIGN(8) e_shmseg_pvt	{
	e_shmseg_t	shm_seg;  /* The shared memory segment */
	uint32_t	refcnt;	  /* host app reference count */
	uint32_t	valid;	  /* 1 if the region is in use, 0 otherwise */
} e_shmseg_pvt_t;

typedef struct ALIGN(8) e_shmtable {
	uint32_t		magic;
	uint32_t		initialized;
	e_shmseg_pvt_t	regions[MAX_SHM_REGIONS];
	uint64_t		free_space;
	uint64_t		next_free_offset;
	uint64_t		paddr_epi;	/* Physical address of the shm region as seen by epiphany */
	uint64_t		paddr_cpu;	/* Physical address of the shm region as seen by the host cpu */
	union {
		uint8_t		*heap;
		uint64_t	__fill1;
	};
	union {
		void		*lock;		/* User-space semaphore (sem_t* on e-hal side) */
		uint64_t	__fill2;
	};
} e_shmtable_t;

#pragma pack(pop)

/** Attach to a shared region identifiable by name */
int e_shm_attach(e_memseg_t *mem, const char* name);

/** Release a shared region allocated with e_shm_attach() */
int e_shm_release(const char* name);

#ifdef __cplusplus
}
#endif

#endif	  /* _E_SHM_H_ */
