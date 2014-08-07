/*
 * a_trace.c
 *
 *  Created on: Feb 3, 2014
 *      Author: M Taveniku
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <e-hal.h>
#include "a_trace_shared.h"
#include "a_trace.h"

/**
 *   Internal helper function definitions
 */

/**
 * Prints the platform information to the terminal
 * @param platformInfo: pointer to a platform info structure
 */
void tracePrintPlatformInfo(e_platform_t *platformInfo);

/**
 * Internal test function to setup E-Cores
 */
int test_setup_e_cores();

/**
 * Global hidden variables used in the trace module
 */
static int traceFileHdl; // handle to our trace file
static volatile unsigned long long **traceBufRdPtr = 0; // first unread position in trace buffer
static unsigned long long **traceBufStart = 0; // start of trace buffer
static unsigned long long **traceBufEnd = 0;   // end address of trace buffer
static 	struct timeval traceStartTime = { 0, 0 };    // when we called start
static unsigned long long traceEventCnt = 0; // how many events have we written
static unsigned traceNumCores = 0;
static unsigned traceSingleNextCore = 0;  // NextCore to check for data reading single
static unsigned traceMultiNextCore = 0;  // next core to check for data reading multiple

// The trace buffer
static e_mem_t      traceBufMem;

/* ********************************************************************************************************
 *
 *  Implementation
 *
 * ******************************************************************************************************* */

/**
 * trace_init - Initializes shared memory areas and local variables
 * This function must have completed before any calls to trace
 * functions are done in e-cores
 */
int trace_init()
{
	unsigned     cnt;
	unsigned     coreTraceBufSz;
	e_platform_t platform; // platform information

	e_set_host_verbosity(H_D0);

	if ( E_OK != e_init(0) ) {
		fprintf(stderr, "Failed to initialize epiphany HAL.\n");
		return E_ERR;
	}

    if ( E_OK != e_shm_alloc(&traceBufMem, HOST_TRACE_SHM_NAME,
							 HOST_TRACE_BUF_SIZE) ) {
	    fprintf(stderr, "Failed to allocate shared memory. Error is %s\n",
				strerror(errno));
		return E_ERR;
	}

	memset((void *)traceBufMem.base, 0, HOST_TRACE_BUF_SIZE); // zero memory

	// figure out how many cores we have
	e_get_platform_info(&platform);
	traceNumCores = platform.rows * platform.cols;

	/*
	 * Get pointers to all traceNumCores buffers
	 */
	traceBufStart = (unsigned long long **)malloc(traceNumCores * sizeof(unsigned long long *));
	traceBufEnd   = (unsigned long long **)malloc(traceNumCores * sizeof(unsigned long long *));
	traceBufRdPtr = (volatile unsigned long long **)malloc(traceNumCores * sizeof(unsigned long long *));
	coreTraceBufSz = HOST_TRACE_BUF_SIZE/traceNumCores; // 16 cores

	for(cnt=0;cnt<traceNumCores;cnt++){
		traceBufStart[cnt] = traceBufMem.base + (coreTraceBufSz*cnt); // start mapped space
		traceBufEnd[cnt] = traceBufStart[cnt] + coreTraceBufSz;  //pointer to past end of buffer
		traceBufRdPtr[cnt] = traceBufStart[cnt]; // initialize read ptr to start of buffer
	}
	traceEventCnt = 0;        // initialize event counter
	traceFileHdl = -1;        // initialize file handle
	traceSingleNextCore = 0;  // initialize where to start
	traceMultiNextCore = 0;   // where to start reading multiple

	return E_OK;
}

void trace_finalize()
{
	e_shm_release(HOST_TRACE_SHM_NAME);
	e_finalize();
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
	gettimeofday(&nowTime, 0); // get start time
	return ((unsigned long long)nowTime.tv_sec * (1000L)) + ((unsigned long long)nowTime.tv_usec / 1000L);
}

/**
 * trace_read -
 * Reads the oldest trace record from the trace buffer, with a timeout
 * if the function times out 0 is returned.
 * @param timeout_millis - how long to block (0 to 2^32 ms)
 * @return - oldest data or 0 on timeout/or empty
 */
unsigned long long trace_read(unsigned timeout_millis)
{
	unsigned done = 0, coreCnt;
	unsigned long long dta;
	unsigned long long tmp1, timeoutTime;
	// when do we timeout
	tmp1 = getTimeMillis();
	timeoutTime = tmp1 + (unsigned long long)timeout_millis;

	//fprintf(stderr,"Times 0x%llx to 0x%llx delta %lld\n", tmp1, timeoutTime, timeoutTime-tmp1);
	while(!done){
		coreCnt = 0;
		while(!done && coreCnt < traceNumCores){
			if(*traceBufRdPtr[traceSingleNextCore] != 0){
				dta = *traceBufRdPtr[traceSingleNextCore];
				*traceBufRdPtr[traceSingleNextCore] = 0;
				traceBufRdPtr[traceSingleNextCore]++;
				if(traceBufRdPtr[traceSingleNextCore] >= traceBufEnd[traceSingleNextCore])
					traceBufRdPtr[traceSingleNextCore] = traceBufStart[traceSingleNextCore];
				done = 1;
			}
			// check next core, or if we are done make sure the next core will be
			// checked next time (fairness)
			traceSingleNextCore++;
			if(traceSingleNextCore >= traceNumCores) traceSingleNextCore = 0;
			coreCnt++; //increment cores checked
		}
		// check if we didn't find data see if we should timeout
		if(!done){
			if (timeoutTime >= getTimeMillis()) {
				usleep(10000); // 10ms sleep
			} else {
				dta = 0;
				done = 1;
			}
		}
	}

	{   // Debugging
		/* unsigned long long tmp2 = getTimeMillis(); */
		/* fprintf(stderr,"Trace Rd: NextCore: %d Data: 0x%llx Time: 0x%llx " */
		/* 		"delta(t): %lld\n", traceSingleNextCore, dta, tmp2, tmp2-tmp1); */
	}
	
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
	unsigned dtaCnt, coreCnt, done; //number of data read

	done = 0;
	coreCnt = 0;
	dtaCnt = 0;
	while(dtaCnt < max_data && coreCnt < traceNumCores && !done) {
		// Debugging output
		//fprintf(stderr,"trace_read core %d: Ptr=%p Data %016llx\n", traceMultiNextCore,
		//		traceBufRdPtr[traceMultiNextCore], *traceBufRdPtr[traceMultiNextCore]);
		dtaCnt += trace_read_coreNo_n(&(buffer[dtaCnt]), max_data - dtaCnt, traceMultiNextCore);
		if(dtaCnt >= max_data) done = 1; // we are done
		traceMultiNextCore++; // next time look at the next core
		if(traceMultiNextCore >=traceNumCores) traceMultiNextCore = 0;
		coreCnt++; // increment tested number of cores
	}

	// Debugging output
	//fprintf(stderr,"Number buffers checked: %d NumData: %d\n", coreCnt, dtaCnt);

	return dtaCnt;
}

/**
 * trace_read_n - reads a chunk of data from the trace buffer
 * this is a non-blocking function that returns immediately
 * @param buffer - buffer for data
 * @param max_data - max number of events to read
 * @param coreNo - this cores buffer 0 .. max-cores
 * @return number of double words read
 */
int trace_read_coreNo_n(unsigned long long *buffer, unsigned max_data, unsigned coreNo)
{
	unsigned cnt; //number of data read
	cnt = 0;
	while(cnt < max_data && *traceBufRdPtr[coreNo] != 0) {
		// make a small delay so that e-core can finish its write if it is the middle

		// Debugging output
		//fprintf(stderr,"RD_Core %d: Ptr=%p Data %016llx\n", coreNo, traceBufRdPtr[coreNo], *traceBufRdPtr[coreNo]);

		*buffer = *traceBufRdPtr[coreNo];
		*traceBufRdPtr[coreNo] = 0; //make sure it is 0x0
		cnt++;
		buffer++;
		traceBufRdPtr[coreNo]++;
		if(traceBufRdPtr[coreNo] >= traceBufEnd[coreNo]) traceBufRdPtr[coreNo] = traceBufStart[coreNo];
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
	//fprintf(stderr,"trace event to string 0x%llx ", event);
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
 * @return the event as unsigned long long
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
 * trace_stop - stops the tracing from host - no further calls to trace allowed
 * @return - always succeeds
 */
void trace_stop()
{
	free(traceBufStart);
	free(traceBufEnd);
	free(traceBufRdPtr);

	traceBufStart = NULL;
	traceBufEnd = NULL;
	traceBufRdPtr = NULL;
}

/**
 * Open the trace file on disk with the default filename and option field
 * It also initiates the file and writes the header information
 * @param optionField - the optional text field to filename
 * @return zero on success
 */
int trace_file_open(char *optionField)
{
	unsigned hdrBuf[32]; // 128 byte file header
	char fName[1024];
	char timeStr[255]; // temporary string for filename
	time_t tNow = time(0); // get current time in seconds
	struct tm *calTime = localtime(&tNow); // get local time as a calendar item
	strftime(timeStr, 255, "%Y%m%d_%H%M%S", calTime);

	if(optionField == 0 || strlen(optionField)==0 || strlen(optionField) > 100) {
		snprintf(fName,1024,"trace_%s.etr", timeStr);
	} else {
		snprintf(fName,1024,"trace_%s_%s.etr", timeStr, optionField);
	}
	traceFileHdl = open(fName,O_CREAT|O_APPEND|O_RDWR, 0x1FF);

	if(traceFileHdl <= 0) {
	  fprintf(stderr,"Error opening file %s: %s", fName, strerror(errno));
	  return -1;
	}
	// fill in header information
	memset(hdrBuf,0,128); // clear header
	hdrBuf[0] = 0xE3ACE001;
	hdrBuf[1] = 128;  // start data offset 128 Byte
	hdrBuf[2] = 0x01; //trace file version
	hdrBuf[3] = 0x02; //trace definition version
	hdrBuf[4] = 0xE3ACE002;
	hdrBuf[5] = traceStartTime.tv_sec;
	hdrBuf[6] = traceStartTime.tv_usec;
	write(traceFileHdl,hdrBuf,128); // write header to buffer
	//fprintf(stderr,"Created file %s handle is %2d\n",fName, traceFileHdl );
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
	if(retVal < (int)sizeof(event)) fprintf(stderr,"short write");
	traceEventCnt++; //increment counter
	return 0;
}


/**
 * trace_file_write write an event to the file
 * @param *event - buffer of events to write
 * @param cnt - number of events to write
 * @return the number of events written (-1 on error)
 */
int trace_file_write_n(unsigned long long *event, int cnt)
{
	int bWr, bytesToWrite;
	int done;
	int retryCnt;
	if(traceFileHdl < 0) {
		fprintf(stderr,"Error tried writing to file %d\n", traceFileHdl );
		return -1;
	}
	bytesToWrite = sizeof(unsigned long long) * cnt; // total bytes to write
	bWr = 0;
	retryCnt = 0;
	done = 0;
	while(!done && retryCnt < 5) {
		bWr = write(traceFileHdl, event, sizeof(unsigned long long)*cnt);
		if(bWr < 0 ) {
			// we got an error just return with -1;
			fprintf(stderr,"Error write failed with %s\n", strerror(errno));
			return -1;
		}
		if(bWr < bytesToWrite) {
			fprintf(stderr,"short write");
			bytesToWrite -=bWr; // this is how many we have left to write
			retryCnt++; // limit retries a bit
			usleep(1000); // sleep a ms just to be nice to disk
		} else {
			done = 1; // we are done
		}
	}
	traceEventCnt += (bWr/8); // Number of 64 bit events written
	return ((sizeof(unsigned long long)*cnt) - bytesToWrite);
}

/**
 * Open a trace file and send to out file in text form
 * @param inFileName - name of trace file to read
 * @param outFileName - name of textual trace file to write
 */
int trace_file_read_open(char *inFileName, char *outFileName)
{
	struct stat iFileStat;
	unsigned hdr[32];
	unsigned nEvents;
	unsigned ftr[6];
	unsigned cnt;
	unsigned long long theEvent;
	char eventBuf[1024]; // big buffer for string
	int     wrCnt;
	size_t rdCnt;
	FILE *iFile, *oFile;
	int retVal;

	// check that the input file exists and has data // header and footer is 32 + 6, 4 byte words = 152 byte
	// read the header
	if(inFileName == NULL || outFileName == NULL || strlen(inFileName)==0 || strlen(outFileName)==0){
		fprintf(stderr,"Invalid input or output file name\n");
		return -1;
	}
	// check that file exists
	retVal = stat(inFileName, &iFileStat);
	if(retVal < 0 ) {
		// we could not stat the file
		fprintf(stderr,"Could not stat input file %s\n", inFileName);
	}
	// Check that file has data
	if(iFileStat.st_size < (32+6)*4) {
		fprintf(stderr,"Not enough data in file \n");
		return -1;
	}
	// Now we can open the input stream
	iFile = fopen(inFileName,"rb");
	if(iFile == NULL) {
		fprintf(stderr,"Error opening input file %s \n", inFileName);
		return -1;
	}
	oFile = fopen(outFileName,"wb");
	if(outFileName == NULL) {
		fprintf(stderr,"Could not open output file %s for writing \n", outFileName);
		fclose(iFile);
		return -1;
	}
	// lets figure out how many events we have
	nEvents = (iFileStat.st_size - ((32+6)*4)) / 8 ; // hdr:e:e:e:e .. :ftr eof
	// Read Header
	rdCnt = fread(hdr,4,32, iFile);
	if(rdCnt != 32) {
		fprintf(stderr,"Error reading header RetVal:%lu\n", (unsigned long)rdCnt);
		fclose(iFile);
		fclose(oFile);
		return -1;
	}
	wrCnt = fprintf(oFile,"Header: %08x %08x %08x %08x %08x %08x\n", hdr[0],hdr[1],hdr[2],hdr[3],hdr[4],hdr[5]);
	if(wrCnt < 0) {
		fprintf(stderr,"Error writing file header \n");
		fclose(iFile);
		fclose(oFile);
		return -1;
	}
	// Now we should read the data
	for(cnt=0;cnt<nEvents;cnt++){
		if(cnt % 1000 == 0) {
			// little console output
			fprintf(stderr,"%u.",cnt/1000);
		}
		rdCnt = fread(&theEvent,8,1, iFile);
		if(rdCnt != 1) {
			fprintf(stderr,"Bad read at event %u returned %lu\n",cnt, (unsigned long)rdCnt);
			fclose(iFile);
			fclose(oFile);
			return -1;
		}
		trace_event_to_string(eventBuf, theEvent); // get the string
		wrCnt = fprintf(oFile,"E-No: %u: %s\n", cnt, eventBuf);
		if(wrCnt < 0) {
			//write error
			fprintf(stderr,"Write to file failed at cnt = %u\n", cnt);
			fclose(iFile);
			fclose(oFile);
			return -1;
		}
	}
	// if everything went well this is the footer to go
	rdCnt = fread(ftr,4,6, iFile);
	if(rdCnt != 6) {
		fprintf(stderr,"Error reading footer RetVal:%lu\n", (unsigned long)rdCnt);
		fclose(iFile);
		fclose(oFile);
		return -1;
	}
	fprintf(oFile,"Footer: %08x %08x Event High: %x Event Count: %u Magic: %08x %08x\n",
			ftr[0],ftr[1],ftr[2],ftr[3],ftr[4],ftr[5]);
	fprintf(stderr,"\n");
	return 0;
}





/** ****************************************************************************
 *
 *  TEST ROUTINES
 *
 * *****************************************************************************
 */
#if EnableTests
/**
 * Test program for trace file writing only
 *
 */
int test_write_trace_file()
{
	unsigned long long te;
	int cnt;
	trace_init(); // initialize a trace function
	trace_file_open("test");
	for(cnt=0; cnt<1024;cnt++){
		te = trace_to_event(cnt, cnt+1, cnt+2,cnt+3,cnt+4);
		trace_file_write(te);
	}
	trace_file_close();
	return 0;
}

/**
 * Dumps data from the global trace buffer
 * @param startIdx  - index in buffer 0 ..
 * @param endIdx - last index in buffer to print
 */
int trace_dump_buffer(int startIdx, int endIdx)
{
	unsigned cnt;
	if(startIdx < 0 || &(traceBufStart[0][startIdx]) >= traceBufEnd[traceNumCores-1]) {
		fprintf(stderr,"Index out of bounds Start Index %d\n", startIdx);
		return -1;
	}
	if(startIdx > endIdx || &(traceBufStart[0][endIdx]) >= traceBufEnd[traceNumCores-1]) {
		fprintf(stderr,"Index out of bounds end Index %d\n", endIdx);
		return -1;
	}
	// in order to have good readings we need to dump the buffer before using printf
	unsigned nItems = (endIdx - startIdx)+1; // number of data
	unsigned long long *buf = (unsigned long long *)malloc(sizeof(unsigned long long)*nItems);
	if(buf == NULL) {
		fprintf(stderr,"dump buf could not allocate %d items in memory\n", nItems);
		return -1;
	}
	// got memory copy
	memcpy((void*)buf, (void*)&traceBufStart[0][startIdx], nItems*sizeof(unsigned long long));
	// Calculate timing
	for(cnt = 0; cnt<nItems; cnt++){
		unsigned t1, t2, t3, td1, td2;
		t1 = (unsigned)(buf[cnt] & 0x0FFFFFFFFLL);
		t3 = (unsigned)(buf[cnt+20] & 0x0FFFFFFFFLL);
		if(t3>t2) td2 = t3-t2;
		else td2 = t2-t3;

		if(cnt>0 && cnt !=20) {
			t2 = (unsigned)(buf[cnt-1] & 0x0FFFFFFFFLL);
			td1 = t2-t1;
		} else {
			td1=0;
		}
		fprintf(stderr,"Index:%4d Data: 0x%016llx Delta= %10u, Drift=%8u\n", startIdx+cnt,
				buf[cnt],td1, td2 );
	}
	free(buf); // return memory
	return 0;
}

/**
 * Dumps data from the trace buffer of core coreNo
 * @coreNo - the core buffer we want
 * @param startIdx  - index in buffer 0 ..
 * @param endIdx - last index in buffer to print
 */
int trace_dump_coreNo_buffer(int coreNo, int startIdx, int endIdx)
{
  if(coreNo >= (int)traceNumCores || coreNo < 0){
		fprintf(stderr,"Core Number out of bounds %d [%d .. %d] \n",coreNo, 0, traceNumCores);
		return -1;
	}
	if(startIdx < 0 || &(traceBufStart[coreNo][startIdx]) >= traceBufEnd[coreNo]) {
		fprintf(stderr,"Index out of bounds Start Index %d\n", startIdx);
		return -1;
	}
	if(startIdx > endIdx || &(traceBufStart[coreNo][endIdx]) >= traceBufEnd[coreNo]) {
		fprintf(stderr,"Index out of bounds end Index %d\n", endIdx);
		return -1;
	}
	while(startIdx <= endIdx){
		fprintf(stderr,"Index:%4d Data: 0x%016llx\n", startIdx, traceBufStart[coreNo][startIdx]);
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
int test_read_buf(int argc, char **argv)
{
	char eStr[2048]; // big just in case
	unsigned long long te;
	int cnt;
	trace_init(); // initialize a trace function
	if(argc == 1){
		fprintf(stderr,"starting e-cores argv[0] is %s\n", argv[0]);
		test_setup_e_cores();
		cnt = 0;
		while(1) { // keep going until ctrl-c
			fprintf(stderr,"Waiting a little %d\n", cnt);
			cnt++;
			sleep(5);
			fprintf(stderr,"slept 5 seconds dumping buffer \n");
			trace_dump_buffer(0,40);
			//trace_dump_buffer(1023,1043);
		}
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

/**
 * Test that we can read events fairly from e-cores
 * Must know e-cores work first
 */
int test_simple_buf()
{
	unsigned long long te; // the event
	char buf[2048]; // buffer for the event
	trace_init(); // setup buffers
	test_setup_e_cores(); // start epiphany side
	while(1){
		te = trace_read(1000); // look for event
		if(te != 0){
			trace_event_to_string(buf,te);
			fprintf(stderr, "Got event %s\n", buf);
		} else {
			fprintf(stderr,"Timeout\n");
		}
	}
	return 0;
}

/**
 * Test Reading multiple data across many buffers
 */
int test_read_n()
{
	unsigned long long te[1024]; // the event
	unsigned cnt, i;
	char buf[2048]; // buffer for the event
	trace_init(); // setup buffers
	test_setup_e_cores(); // start epiphany side
	while(1){
		cnt = trace_read_n(te, 14); // look for event
		if(cnt > 0){
			for(i=0;i<cnt;i++){
				trace_event_to_string(buf,te[i]);
				fprintf(stderr, "Got event %s\n", buf);
			}
		} else {
			fprintf(stderr,"Timeout\n");
		}
		sleep(1);
	}
	return 0;
}

/**
 * Test where we start the e-cores to generate events
 * We then write these events to the trace file
 */
int test_write_file()
{
	unsigned long long te[1024]; // the event
	unsigned cnt, i, evCnt;
	char buf[2048]; // buffer for the event
	trace_init(); // setup buffers
	test_setup_e_cores(); // start epiphany side
	// Open a new file
	trace_file_open("test"); // open file with default values
	evCnt = 0;
	while(evCnt < 1000){
		cnt = trace_read_n(te, 14); // look for events uneven on purpose
		if(cnt > 0){
			for(i=0;i<cnt;i++){
				trace_event_to_string(buf,te[i]);
				fprintf(stderr, "Got event %s\n", buf);
			}
			trace_file_write_n(te,cnt);
			evCnt+=cnt;
		} else {
			fprintf(stderr,"Timeout\n");
		}
		sleep(1);
	}
	trace_file_close();
	return 0;

}

/**
 * Convert a binary trace file to a readable text file.
 * Test function only, user need to have a valid input file
 */
int test_read_file()
{
	return trace_file_read_open("trace_20140213_215911_test.etr", "output.txt");
}

/**
 * Starts the arm side software as a daemon
 */
int test_log_daemon()
{
	int done = 0;
	char inBuf[10]; // some dummy input
	int nInCh; // how much we did read
	unsigned long long tb[1024]; // trace event buffer 8k
	unsigned nEvent;
	// change how stdin is used ..

	fcntl(fileno(stdin), F_SETFL,O_NONBLOCK);
	trace_init();
	trace_start();
	trace_file_open("test"); // write header and open trace file
	// get the e-cores started (this would be another program)
	test_setup_e_cores();
	fprintf(stdout,"Starting logger daemon - press any key to stop \n");
	done = 0;
	while(!done) {
		nEvent = trace_read_n(tb,1024);
		if(nEvent > 0) {
			fprintf(stdout,"%d ", nEvent);
			fflush(stdout);
			trace_file_write_n(tb, nEvent);
		} else {
			// No events yet, sleep a little
			usleep(100000); // 100ms before trying again
		}
		nInCh = read(fileno(stdin), inBuf, 10);
		if(nInCh > 0) {
			done = 1;
		}
	}
	fprintf(stdout,"Ending trace capture\n");
	trace_file_close();
	trace_stop();
	return 0;
}

/**
 * Setup and start e-cores for testing
 */
int test_setup_e_cores()
{
	int rowNo, colNo;
	e_mem_t emem; // shared memory for print
	e_epiphany_t dev; // one Epiphany chip
	e_platform_t platform; // platform information

	fprintf(stderr, "Reset EP Chips\n");
	e_reset_system();               // reset Epiphany chip


	fprintf(stderr,"Allocating shared memory\n");
	e_alloc(&emem, SHARED_DATA_BUF_OFFSET, SHARED_DATA_BUF_SIZE); //allocate memory
	memset(emem.base, 0, SHARED_DATA_BUF_SIZE); // clear it

	// Check what kind of platform we have
	e_get_platform_info(&platform);

	fprintf(stderr,"Open a %d by %d device\n", platform.rows, platform.cols);
	e_open(&dev, 0, 0, platform.rows, platform.cols);  //Open the 4 by 4 -core work group

	fprintf(stderr, "Reset %d by %d Cores\n", platform.rows, platform.cols);
// Patch for difference between SDK versions
#ifndef X86_09
	e_reset_group(&dev);
#else
	for( rowNo=0; rowNo<platform.rows;rowNo++){
		for( colNo=0;colNo<platform.cols;colNo++){
			e_reset_core(&dev,rowNo,colNo);  //
		}
	}
#endif
	fprintf(stderr, "Loading Programs\n");

	// Load the device program onto the selected eCore
	for(rowNo=0;rowNo<platform.rows;rowNo++){
		for(colNo=0;colNo<platform.cols;colNo++){
			e_load("e_trace.srec", &dev, rowNo, colNo, 0); // load program for core rowNo colNo
		}
	}
	// Start applications
	fprintf(stderr, "Starting applications\n");
	for(rowNo=0; rowNo<platform.rows;rowNo++){
		for(colNo=0;colNo<platform.cols;colNo++){
			e_start(&dev,rowNo,colNo);  // start processor row,col
		}
	}
	fprintf(stderr,"%d Applications running on %d by %d e_cores\n",
			platform.rows * platform.cols, platform.rows, platform.cols);
	return 0;
}

//#define SIMPLE_BUF_TEST
//#define READ_BUF_TEST
//#define TEST_READ_N
//#define TEST_FILE_WRITE
//#define TEST_FILE_READ
#define TEST_LOG_DAEMON
int test_main(int argc, char **argv)
{
	int retVal = 0;
	//char *arg = "e-cores";
	//int nCores, nRows, nCols;


	retVal = e_init(NULL);  // should have a pointer to the current platform HDF file
	if(retVal != E_OK) {
	  fprintf(stderr,"Failed to initialize Epiphany platform\n");
	  return -1;
	}

	// Get platform information

#ifdef READ_BUF_TEST
	/**
	 * Test of reading from trace buffer with data
	 */
	fprintf(stdout,"Starting Trace Server with Events\n");
	retVal = test_read_buf(1,&arg);
	fprintf(stdout,"Trace Buffer Read Exiting with code %2d\n", retVal);
#endif

#ifdef SIMPLE_BUF_TEST
	fprintf(stdout,"Starting Simple Buf Test\n");
	retVal = test_simple_buf();
	fprintf(stdout,"Simple Buf Test Exiting with code %2d\n", retVal);
#endif
#ifdef TEST_READ_N
	fprintf(stdout,"Starting Read N Test\n");
	retVal = test_read_n();
	fprintf(stdout,"Test Read N Exiting with code %2d\n", retVal);
#endif

#ifdef TEST_FILE_WRITE
	fprintf(stdout,"Starting File Write Test\n");
	retVal = test_write_file();
	fprintf(stdout,"Test Write File Exiting with code %2d\n", retVal);
#endif

#ifdef TEST_FILE_READ
	fprintf(stdout,"Starting Test File Read\n");
	retVal = test_read_file();
	fprintf(stdout,"Test Read File Exiting with code %2d\n", retVal);
#endif
#ifdef TEST_LOG_DAEMON
	fprintf(stdout,"Starting Test Log Daemon\n");
	retVal = test_log_daemon();
	fprintf(stdout,"Test Log Daemon Exiting with code %2d\n", retVal);
#endif
	fprintf(stdout,"Done with test exiting\n");
	return retVal;
}

#endif /* EnableTests */
