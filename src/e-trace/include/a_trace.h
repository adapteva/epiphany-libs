/*
 * a_trace.h
 *
 *  Created on: Feb 6, 2014
 *      Author: M Taveniku
 */

#ifndef A_TRACE_H_
#define A_TRACE_H_



typedef struct trace_event_s {
	unsigned severity;
	unsigned eventId;
	unsigned coreId;
	unsigned breakpoint;
	unsigned data;
	unsigned timestamp;
} trace_event_t;




/**
 * trace_init - Initializes shared memory areas and local variables
 * This function must have completed before any calls to trace
 * functions are done in e-cores
 */
int trace_init();

/**
 * trace_finalize - teardown and release resources. 
 */
void trace_finalize();

/**
 * trace_start - Produces a start time for the trace buffers
 * for a global time-stamp
 */
int trace_start();

/**
 * trace_read - reads a block of trace info
 * reads the oldest trace record from the trace buffer
 * if the isBlock flag is set this function block until data is available
 * or timeout value is reached
 * @param timeout_millis - how long to block (0 = no block)
 * @return - oldest data or 0 on timeout/or empty
 */
unsigned long long trace_read(unsigned timeout_millis);

/**
 * trace_read_n - reads a chunk of data from the trace buffer
 * this is a non-blocking function that returns immediately
 * @param buffer - buffer for data
 * @param max_data - max number of events to read
 * @return number of items read or negative on error
 */
int trace_read_n(unsigned long long *buffer, unsigned max_data);

/**
 * trace_read_n - reads a chunk of data from the trace buffer
 * this is a non-blocking function that returns immediately
 * @param buffer - buffer for data
 * @param max_data - max number of events to read
 * @param coreNo - this cores buffer 0 .. max-cores
 * @return number of double words read
 */
int trace_read_coreNo_n(unsigned long long *buffer, unsigned max_data, unsigned coreNo);

/**
 * trace_event_to_string - creates a string from a trace event
 * @param buf - buffer to put the string
 * @param event - the event to convert
 * @return 0 on success
 */
int trace_event_to_string(char *buf, unsigned long long event);

/**
 * trace_event_to_struct - takes an event and converts
 * to an event structure
 * @param event - the event
 * @param e_struct - pointer to a struct to fill
 * @return 0 on success
 */
int trace_event_to_struct(trace_event_t *e_struct, unsigned long long event);

/**
 * trace_struct_to_event - Converts a trace structure to a binary event
 * @param e_struct - the event as struct
 * @param tEvent - pointer to the event to create (output)
 * @return 0 on success
 */
int trace_struct_to_event(unsigned long long *tEvent, trace_event_t *e_struct);

/**
 * create_event - create a new event based on input
 * @param severity - 0..3
 * @param eventId - 0 .. 63
 * @param breakpoint - 0 .. 15
 * @param data - 0..255
 * @param coreId - 0 .. 4095
 * @return trace_event on success - 0 on fail
 */
unsigned long long trace_to_event(unsigned severity, unsigned eventId,
		unsigned breakpoint, unsigned coreId, unsigned data);
/**
 * trace_stop - stops the tracing from host - no further calls to trace allowed
 * @return - always succeeds
 */
void trace_stop();

/**
 * Open the trace file on disk with the default filename and option field
 * It also initiates the file and writes the header information
 * @param optionField - the optional text field to filename
 * @return zero on success
 */
int trace_file_open(char *optionField);

/**
 * close the trace file and write the trailer to it
 */
int trace_file_close();

/**
 * trace_file_write
 * @param event - the event to write
 * @return 0 on success -1 on error
 */
int trace_file_write(unsigned long long event);

/**
 * trace_file_write write an event to the file
 * @param *event - buffer of events to write
 * @param cnt - number of events to write
 * @return the number of events written (-1 on error)
 */
int trace_file_write_n(unsigned long long *event, int cnt);

/**
 * Open a trace file and send to outfile
 * @param inFileName - name of trace file to read
 * @param outFileName - name of textual trace file to write
 */
int trace_file_read_open(char *inFileName, char *outFileName);





#endif /* A_TRACE_H_ */
