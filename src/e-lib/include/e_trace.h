/*
 * e_trace.h
 *
 *  Created on: Oct 1, 2013
 *      Author: adadmin
 */

#ifndef E_TRACE_H_
#define E_TRACE_H_

/**
 * Define constants to be used in trace buffer from Epiphany
 */
/** Make them two most significant 32 bits of word */
#define T_SEVERITY_ERROR 0
#define T_SEVERITY_WARNING (1 << 30)
#define T_SEVERITY_NOTICE (2 << 30)
#define T_SEVERITY_DEBUG (3 << 30)

/**
 * Event_ID
 * These are bits 24 .. 29
 */
#define T_EVENT_PROGRAM_START (0 << 24)
#define T_EVENT_PROGRAM_STOP (1 << 24)
#define T_EVENT_PROCESSING_START (2 << 24)
#define T_EVENT_PROCESSING_STOP (3 << 24)
#define T_EVENT_WRITE_START (4 << 24)
#define T_EVENT_WRITE_STOP (5<<24)
#define T_EVENT_READ_START (6 << 24)
#define T_EVENT_READ_STOP (7<<24)
#define T_EVENT_USER1_START (8<<24)
#define T_EVENT_USER1_END (9<<24)
#define T_EVENT_MARKER1 (10<<24)
#define T_EVENT_CONFIGURATION (31 << 24) /* not implemented in e_core */

/**
 * Positions in source code
 * 4 bits 20-23
 */
#define T_BP_0 (0 <<20)
#define T_BP_1 (1 <<20)
#define T_BP_2 (2 <<20)
#define T_BP_3 (3 <<20)
#define T_BP_4 (4 <<20)
#define T_BP_5 (5 <<20)
#define T_BP_6 (6 <<20)
#define T_BP_7 (7 <<20)
#define T_BP_8 (8 <<20)
#define T_BP_9 (9 <<20)
#define T_BP_10 (10 <<20)
#define T_BP_11 (11 <<20)
#define T_BP_12 (12 <<20)
#define T_BP_13 (13 <<20)
#define T_BP_14 (14 <<20)
#define T_BP_15 (15 <<20)

#define TRACE_FILE_MAGIC (0xE3ACE)
/**
 * Initialize data structures, call this first before using trace functions
 */
int trace_init();
/**
 * This function starts the clock counter, tracing can be used after this call
 */
int trace_start();
/**
 * Start trace, but wait until all processors on this chip are ready and waiting to start
 * @param isMaster - one processor on the chip is master and starts the group
 * @param group - array of member coreid for this group
 */
int trace_start_wait_all();
/**
 * Write the event "event" to  log with data
 * @param severity - 0 .. 3
 * @param event - event id
 * @param breakpoint - place in user code that we hit
 * @param data - data associated with the event
 */
int trace_write(unsigned severity, unsigned event, unsigned breakpoint, unsigned data);

/**
 * stop this trace, free resources
 */
int trace_stop();


#endif /* E_TRACE_H_ */
