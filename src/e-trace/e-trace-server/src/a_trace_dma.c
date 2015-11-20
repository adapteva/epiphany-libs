/*
 * a_trace.c
 *
 *  Created on: Feb 3, 2014
 *      Author: adadmin
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
#include "sharedData.h"
#include <e-hal.h>


typedef struct trace_event_s {
	unsigned severity;
	unsigned eventId;
	unsigned coreId;
	unsigned breakpoint;
	unsigned data;
	unsigned timestamp;
} trace_event_t;

int test_setup_e_cores(); // test program
/**
 * Prints the platform information to the terminal
 * @param platformInfo: pointer to a platform info structure
 */
void tracePrintPlatformInfo(e_platform_t *platformInfo);

/**
 * trace_init - Initializes shared memory areas and local variables
 * This function must have completed before any calls to trace
 * functions are done in e-cores
 */
int trace_init();

/**
 * trace_start - Produces a start time for the trace buffers
 * for a global timestamp
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
 */
int trace_read_n(unsigned long long *buffer, unsigned max_data);

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
 * trace_stop - stops the tracing from host
 * @return - zero on success
 */
int trace_stop();

/**
 * Open the trace file on disk
 * @return zero on success
 */
int trace_file_open();

/**
 * close the trace file and write the trailer to it
 */
int trace_file_close();

/**
 * trace_file_write
 * @param event - the event to write
 * @return 0 on success
 */
int trace_file_write(unsigned long long event);


/**
 * Global hidden variables used in the trace module
 */
static int traceFileHdl; // handle to our trace file
static unsigned long long *traceBufRdPtr; // first unread position in trace buffer
static unsigned long long *traceBufStart; // start of trace buffer
static unsigned long long *traceBufEnd;   // end address of trace buffer
static e_mem_t traceBufMem;               // pointer to memory of the trace buffer
static 	struct timeval traceStartTime;    // when we called start
static unsigned long long traceEventCnt; // how many events have we written


/* ********************************************************************************************************
 *
 *  Implementation
 *
 * ******************************************************************************************************* */

/**
 * Test program for trace file writing only
 *
 */
int main_trace_file(int argc, char **argv)
{
	unsigned long long te;
	int cnt;
	trace_init(); // initialize a trace function
	trace_file_open();
	for(cnt=0; cnt<1024;cnt++){
		te = trace_to_event(cnt, cnt+1, cnt+2,cnt+3,cnt+4);
		trace_file_write(te);
	}
	trace_file_close();
	return 0;
}

/**
 * Dumps data from the trace buffer
 * @param startIdx  - index in buffer 0 ..
 * @param endIdx - last index in buffer to pront
 */
int trace_dump_buffer(int startIdx, int endIdx)
{
	int cnt;
	if(startIdx < 0 || &(traceBufStart[startIdx]) >= traceBufEnd) {
		fprintf(stderr,"Index out of bounds Start Index %d\n", startIdx);
		return -1;
	}
	if(startIdx > endIdx || &(traceBufStart[endIdx]) >= traceBufEnd) {
		fprintf(stderr,"Index out of bounds end Index %d\n", endIdx);
		return -1;
	}
	while(startIdx <= endIdx){
		fprintf(stderr,"Index:%4d Data: 0x%016llx\n", startIdx, traceBufStart[startIdx]);
		startIdx++;
	}
	return 0;
}
/**
 * Prints the platform information to the terminal
 * @param platformInfo: pointer to a platform info structure
 */
void tracePrintPlatformInfo(e_platform_t *platformInfo)
{
	if(platformInfo == NULL) {
		printf("Platform Info Structure is NULL\n");
		return;
	}
	printf("Platform Info: \n\tVersion = %s, \n\tRowStart %2u ColStart %2u Rows %2u Cols %2u\n\tChips %2d Emem %4d\n",
			platformInfo->version, platformInfo->row, platformInfo->col, platformInfo->rows,
			platformInfo->cols, platformInfo->num_chips, platformInfo->num_emems);
	return;
}

/**
 * Test program for trace buffer reading
 *
 */
int main_read_buf(int argc, char **argv)
{
	char eStr[2048]; // big just in case
	unsigned long long te;
	int cnt;
	trace_init(); // initialize a trace function
	if(argc == 1){
		fprintf(stderr,"starting e-cores argv[0] is %s\n", argv[0]);
		test_setup_e_cores();
		fprintf(stderr,"Waiting a little \n");
		sleep(600);
		fprintf(stderr,"slept 3 seconds dumping buffer \n");
		trace_dump_buffer(0,64);
		//trace_dump_buffer(1023,1043);
	} else {
		fprintf(stderr,"Main Read Buf\n");
		while(cnt < 30) {
			te = trace_read(1000); // second timeout
			if(te != 0) {
				// got an event
				trace_event_to_string(eStr,te);
				fprintf(stdout,"Event: 0x%llx = %s\n", te, eStr);
			} else {
				// got timeout
				fprintf(stdout,"timeout - cnt=%d\n", cnt);
			}
			cnt++;
		}
	}
	fprintf(stdout,"Done Exiting\n");
	return 0;
}

int test_setup_e_cores()
{
	int rowNo, colNo;
	e_platform_t platform; // platform information
	e_mem_t emem; // shared memory for print
	e_epiphany_t dev; // one epiphany chip

	fprintf(stderr, "Reset EP Chips\n");
	e_reset_system();               // reset epiphany chip

	fprintf(stderr, "Get Platform Info\n");
	e_get_platform_info(&platform); // get the platform information
	tracePrintPlatformInfo(&platform);

	fprintf(stderr,"Allocating shared memory\n");
	e_alloc(&emem, SHARED_DATA_BUF_OFFSET, SHARED_DATA_BUF_SIZE); //alloc memory
	memset(emem.base, 0, SHARED_DATA_BUF_SIZE); // clear it
	fprintf(stderr,"Open a 4 by 4 device\n");
	e_open(&dev, 0, 0, 4, 4);  //Open the 4 by 4 -core work group

	fprintf(stderr, "Reset 4 by 4 Cores\n");
// Patch for difference between SDK versions
#ifndef X86_09
	e_reset_group(&dev);
#else
	for( rowNo=0; rowNo<4;rowNo++){
		for( colNo=0;colNo<4;colNo++){
			e_reset_core(&dev,rowNo,colNo);  //
		}
	}
#endif
	fprintf(stderr, "Loading Programs\n");

	// Load the device program onto the selected eCore
	for(rowNo=0;rowNo<4;rowNo++){
		for(colNo=0;colNo<4;colNo++){
			e_load("e_trace.srec", &dev, rowNo, colNo, 0); // load program for core rowNo colNo
		}
	}
	// Start applications
	fprintf(stderr, "Starting applications\n");
	for(rowNo=0; rowNo<4;rowNo++){
		for(colNo=0;colNo<4;colNo++){
			e_start(&dev,rowNo,colNo);  // start processor row,col
		}
	}
	fprintf(stderr,"Applications running\n");
	return 0;
}

int main(int argc, char **argv)
{
	int retVal = 0;
	char *arg = "e-cores";
	retVal = e_init(NULL);  // should have a pointer to the current platform HDF file
	if(retVal != 0) {
		fprintf(stderr,"Failed to initialize Epiphany platform\n");
		return -1;
	}
	/**
	 * Test of writing to trace file
	 */
	//fprintf(stdout,"Starting Trace File Write Test\n");
	//retVal = main_trace_file(0,0);
	//fprintf(stdout,"Trace File Test Exit with code %2d\n", retVal);

	/**
	 * Test of reading from trace buffer with no data
	 */
	//fprintf(stdout,"Starting Trace Server without Events\n");
	//retVal = main_read_buf(0,0);
	//fprintf(stdout,"Trace Buffer Read Empty Exiting with code %2d\n", retVal);

	/**
	 * Test of reading from trace buffer with data
	 */
	fprintf(stdout,"Starting Trace Server with Events\n");
	retVal = main_read_buf(1,&arg);
	fprintf(stdout,"Trace Buffer Read Exiting with code %2d\n", retVal);

	return retVal;
}


/**
 * trace_init - Initializes shared memory areas and local variables
 * This function must have completed before any calls to trace
 * functions are done in e-cores
 */
int trace_init()
{
	// get memory pointers
	e_alloc(&traceBufMem, HOST_TRACE_BUF_OFFSET, HOST_TRACE_BUF_SIZE);
	traceBufStart = traceBufMem.base; // start mapped space
	traceBufEnd = traceBufStart + HOST_TRACE_BUF_SIZE;  //pointer to past end of buffer
	traceBufRdPtr = traceBufStart; // initialize read ptr to start of buffer
	// just to make certain (mmap should have done this) clear memory
	memset((void *)traceBufRdPtr, 0 , HOST_TRACE_BUF_SIZE); // zero memory

	traceEventCnt = 0; // initialize counter
	traceFileHdl = -1; // initialize file handle
	return 0;
}

/**
 * trace_start - Produces a start time for the trace buffers
 * for a global timestamp
 */
int trace_start()
{
	gettimeofday(&traceStartTime, 0);
	return 0;
}

/**
 * Return a 64-bit representation of current time in millis
 */
unsigned long long getTimeMillis()
{
	struct timeval nowTime;
	//unsigned long long timeValSec, timeValmSec;
	gettimeofday(&nowTime, 0); // get start time
	//timeValSec = nowTime.tv_sec;
	//timeValSec = timeValSec * 1000L; // sec to millis
	//timeValmSec = nowTime.tv_usec;
	//timeValmSec = timeValmSec / 1000L;
	return ((unsigned long long)nowTime.tv_sec * (1000L)) + ((unsigned long long)nowTime.tv_usec / 1000L);
}

/**
 * trace_read - reads a block of trace info
 * reads the oldest trace record from the trace buffer
 * if the isBlock flag is set this function block until data is available
 * or timeout value is reached
 * @param timeout_millis - how long to block (0 to 2^32 ms)
 * @return - oldest data or 0 on timeout/or empty
 */
unsigned long long trace_read(unsigned timeout_millis)
{
	int done = 0;
	unsigned long long dta;
	unsigned long long tmp2, tmp1, timeoutTime;
	// when do we timeout
	tmp1 = getTimeMillis();
	timeoutTime = tmp1 + (unsigned long long)timeout_millis;
	fprintf(stderr, "size of unsigned %d ulong %d ulonglong %d\n",
			sizeof(unsigned), sizeof(unsigned long), sizeof(unsigned long long));

	fprintf(stderr,"Times 0x%llx to 0x%llx delta %lld\n", tmp1, timeoutTime, timeoutTime-tmp1);
	while(!done){
		if(*traceBufRdPtr != 0){
			dta = *traceBufRdPtr;
			*traceBufRdPtr = 0;
			traceBufRdPtr++;
			if(traceBufRdPtr >= traceBufEnd) traceBufRdPtr = traceBufStart;
			done = 1;
		} else {
			if(timeoutTime >= getTimeMillis()) {
				usleep(10000); // 10ms sleep
			} else {
				dta = 0;
				done = 1;
			}
		}
	}
	tmp2 = getTimeMillis();
	fprintf(stderr,"TrRd1-exit Data: 0x%llx Time: 0x%llx delta(t): %lld\n", dta, tmp2, tmp2-tmp1);
	return dta;
}

/**
 * trace_read_n - reads a chunk of data from the trace buffer
 * this is a non-blocking function that returns immediately
 * @param buffer - buffer for data
 * @param max_data - max number of events to read
 * @return number of double words read
 */
int trace_read_n(unsigned long long *buffer, unsigned max_data)
{
	int cnt; //number of data read
	cnt = 0;
	while(cnt < max_data && *traceBufRdPtr != 0) {
		*buffer = *traceBufRdPtr;
		cnt++;
		buffer++;
		traceBufRdPtr++;
		if(traceBufRdPtr >= traceBufEnd) traceBufRdPtr = traceBufStart;
	}
	return cnt;
}


/**
 * trace_event_to_string - creates a string from a trace event
 * @param buf[255] - buffer to put the string
 * @param event - the event to convert
 * @return 0 on success
 */
int trace_event_to_string(char *buf, unsigned long long event)
{
	trace_event_t te;
	fprintf(stderr,"trace event to string 0x%llx ", event);
	trace_event_to_struct(&te, event);
	sprintf(buf, "CoreId: 0x%03x Time: %8u, Severity: %1u, EventId: %3u, bp: %1u, data: %3u",
			te.coreId, te.timestamp, te.severity, te.eventId, te.breakpoint, te.data);
	return 0;
}

/**
 * trace_event_to_struct - takes an event and converts
 * to an event structure
 * @param event - the event
 * @param e_struct - pointer to a struct to fill
 * @return 0 on success
 */
int trace_event_to_struct(trace_event_t *e_struct, unsigned long long event)
{
	unsigned tmp = (unsigned)((event >> 32));

	e_struct->severity   = (tmp >> 30) & 0x3L; // bit 30,31
	e_struct->eventId    =  (tmp >> 24) & 0x3F; // bit 24 .. 29
	e_struct->coreId     = (tmp >> 8 ) & 0x0FFF; // bit 8 to 19
	e_struct->breakpoint = (tmp >> 20) & 0xF; // bit 20 .. 23
	e_struct->data       = (tmp) & 0xFF; // bit 0 .. 7
	e_struct->timestamp  = (unsigned)((event & 0xFFFFFFFFL));
	return 0;
}


/**
 * trace_struct_to_event - Converts a trace structure to a binary event
 * @param eventStruct - the event
 * @param tEvent - pointer to the event to create
 * @return 0 on success
 */
int trace_struct_to_event(unsigned long long *tEvent, trace_event_t *e_struct)
{
	unsigned tmp;
	tmp = (e_struct->severity & 0x3)  << 30;          // bit 30,31
	tmp = tmp | ((e_struct->eventId & 0x3F) << 24);   // bit 24 .. 29
	tmp = tmp | ((e_struct->coreId & 0xFFF) << 8);    // bit 8 to 19
	tmp = tmp | ((e_struct->breakpoint & 0xF) << 20); // bit 20 .. 23
	tmp = tmp | ((e_struct->data & 0xFF) << 0);       // bit 0 .. 7
	*tEvent = (unsigned long long)tmp << 32; // shift to upper 32 bit
	*tEvent = *tEvent +	e_struct->timestamp;
	return 0;
}

/**
 * create_event - create a new event based on input
 * @return the event
 */
unsigned long long trace_to_event(unsigned severity, unsigned eventId,
		unsigned breakpoint, unsigned coreId, unsigned data)
{
	trace_event_t e_struct;
	unsigned long long ev;
	e_struct.severity   = severity & 0x003;   // 2 bit
	e_struct.eventId    = eventId  & 0x03F;   // 6 bit
	e_struct.coreId     = coreId   & 0xFFF;   // 12 bit
	e_struct.breakpoint = breakpoint & 0x00F; // 4 bit
	e_struct.data       = data     & 0x0FF;   // 8 bit
	e_struct.timestamp  = (unsigned)(getTimeMillis() & 0xFFFFFFFFL);
	trace_struct_to_event(&ev, &e_struct);
	return ev;
}

/**
 * trace_stop - stops the tracing from host
 */
int trace_stop()
{
	return 0;
}

/**
 * Create a new trace file on disk
 * Opens the trace file and keeps internal handle
 */
int trace_file_open()
{
	unsigned hdrBuf[32]; // file header
	char fName[1024];
	struct timeval tv;
	int fh;
	char fname[255]; // filename string
	char timeStr[255]; // temporary string for filename
	time_t tNow = time(0); // get current time in seconds
	struct tm *calTime = localtime(&tNow); // get local time as a calendar item
	strftime(timeStr, 255, "%Y%m%d_%H%M%S", calTime);
	snprintf(fName,1024,"trace_%s_%s.etr", timeStr, "test");

	gettimeofday(&tv, 0); // current time
	traceFileHdl = open(fName,O_CREAT|O_APPEND|O_RDWR, 0x1FF);

	if(traceFileHdl <= 0) {
		fprintf(stderr,"Error opening file %s", fName);
		return -1;
	}
	// fill in header information
	hdrBuf[0] = 0xE3ACE001;
	hdrBuf[1] = 0x80;  // start data offset 128 Byte
	hdrBuf[2] = 0x01; //trace file version
	hdrBuf[3] = 0x02; //trace definition version
	hdrBuf[4] = 0xE3ACE002;
	hdrBuf[5] = tv.tv_sec;
	hdrBuf[6] = tv.tv_usec;
	write(traceFileHdl,hdrBuf,0x80); // write header to buffer
	fprintf(stderr,"Created file %s handle is %2d\n",fName, traceFileHdl );
	return 0;
}

/**
 * close the trace file and write the trailer to it
 */
int trace_file_close()
{
	unsigned trlBuf[6];
	trlBuf[0] = 0xE3ACE001;
	trlBuf[1] = 0xE3ACE001;
	trlBuf[2] = (unsigned)(traceEventCnt >>32);
	trlBuf[3] = (unsigned)(traceEventCnt & 0xFFFFFFFF);
	trlBuf[4] = 0xE3ACE001;
	trlBuf[5] = 0xE3ACE001;

	if(traceFileHdl < 0) return -1; // invalid handle return
	write(traceFileHdl,trlBuf, sizeof(trlBuf));
	close(traceFileHdl);
	traceFileHdl = -1; // invalid
	return 0;
}

/**
 * trace_file_write write an event to the file
 * @param fileHandle - handle to output file
 * @param event - the event to write
 */
int trace_file_write(unsigned long long event)
{
	int retVal;
	if(traceFileHdl < 0) {
		fprintf(stderr,"Error tried writing to file %d\n", traceFileHdl );
		return -1;
	}
	retVal = write(traceFileHdl, &event, sizeof(event));
	if(retVal < 0 ) {
		// we got an error just return with -1;
		fprintf(stderr,"Error write failed with %s\n", strerror(errno));
		return -1;
	}
	if(retVal < sizeof(event)) fprintf(stderr,"short write");
	traceEventCnt++; //increment counter
	return 0;
}

