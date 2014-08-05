/*
 * a_trace_shared.h
 *
 *  Created on: Apr 27, 2013
 *      Author: adadmin
 */

#ifndef E_SHAREDDATA_H_
#define E_SHAREDDATA_H_

//#define SHARED_DATA_HOST_MEM_START (0x1E000000)
//#define SHARED_DATA_DEVICE_MEM_START (0x8E000000)

/**
 * Trace buffer definitions for debug trace.
 * allocate 1MByte buffer and have core 32,8
 */
#define TRACE_MASTER_ID (0x808) /* 32, 8 first core in 16 core */
#define TRACE_COREID_COL_OFFSET (0x040)
#define TRACE_MASTER_BASE (0x80800000) /* base address of master */
#define TRACE_TIMER_FREQ 800 /* parallella */

/**
 * Note:
 * When using the "fast.ldf" linker file the linker will use the first 16MB of shared memory for
 * program code (1MB per core in a 16 core system) and then it will use 8 next MB for program
 * heap (0.5MByte/core)
 * Thus when allocating global shared memory we can only use the uppermost 8MByte safely
 * For the 64Core Chip the program memory is 256k and heap 128K (means there is no shared memory left)
 * Need to change the FAST.LDF to work for us.
 * another alternative, is that since all (malloc:ed data is shared it is possible to have the cores
 * do all allocation work and then communicate that to each other
 *
 * More notes to come here
 * Offset on parallella 16 should be 16+8M = 24M (0x0010 0000 x 0x18) = 0x0180 0000
 *
 */
#define HOST_TRACE_BUF_SIZE	(0x200000) /* 1024 by 256 by 8 byte buffer (2M) */
#define HOST_TRACE_SHM_NAME "trace_buffer"   /* Shared memory region name */

/**
 * Define the data types and structures contained in the shared buffer
 */
typedef char hostSharedData_t [16][256];

#endif /* E_SHAREDDATA_H_ */
