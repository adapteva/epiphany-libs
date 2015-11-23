/*
 * e_trace_server.c
 *
 *  Created on: Feb 13, 2014
 *      Author: adadmin
 */


#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <e-trace.h>

char *traceVersion = "0.90";


/**
 * Starts the arm software to log trace events from e-cores
 */
int run_log_daemon()
{
	int done = 0;
	char inBuf[10]; // some dummy input
	int nInCh; // how much we did read
	unsigned long long tb[1024]; // trace event buffer 8k
	unsigned nEvent;
	char eString[1024];
	// change how stdin is used ..

	// DEBUG
	fcntl(fileno(stdin), F_SETFL,O_NONBLOCK);
	fflush(stdin);
	fprintf(stdout,"Waiting to start press <return> key to start capture\n");
	while(!done) {
		nInCh = read(fileno(stdin), inBuf, 10);
		if(nInCh > 0) {
			done = 1;
			fflush(stdout);
		} else {
			usleep(10000);
		}
	}

	fprintf(stdout,"Starting capture - press <return> key to stop \n");
	done = 0;
	while(!done) {
		nEvent = trace_read_n(tb,1024);
		if(nEvent > 0) {
		    unsigned cnt;

			for(cnt=0;cnt<nEvent;cnt++){
				trace_event_to_string(eString, tb[cnt]);
				fprintf(stderr,"cnt=%d, %s\n", cnt,eString);
			}
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
	fprintf(stdout,"Ending capture\n");
	return 0;
}

int main(int argc, char **argv)
{
    if ( argc < 2 ) {
	    fprintf(stderr,"invalid arguments\n");
		return -1;
	}

	fprintf(stdout,"Initializing the trace server v%s\n", traceVersion);

	if(trace_init() != 0) {
		fprintf(stderr,"Init Failed\n");
		return -1;
	}

	fprintf(stdout,"Starting the trace server\n");

	if(trace_start()!= 0) {
		fprintf(stderr,"Trace start failed\n");
		return -1;
	}

	fprintf(stdout,"Opening the trace file at %s\n", argv[1]);

	if(trace_file_open(argv[1]) != 0) {
		fprintf(stderr,"Failed to open the trace file\n");
		return -1;
	}

	run_log_daemon(); // Run data capture to the log file until we are done

	if(trace_file_close() != 0) { 
	  fprintf(stderr,"Close Trace File Failed\n");
	}

	trace_stop();

	fprintf(stdout,"Exit trace server\n");

    // Cleanup
	trace_finalize();

	return 0;
}
