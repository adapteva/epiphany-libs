/*
 * traceBinToTxt.c
 *
 *  Created on: Mar 23, 2014
 *      Author: adadmin
 */


#include "e-trace.h"
#include <stdio.h>

int main(int argc, char** argv)
{
	char *infile;
	char *outfile;
	int retval;
	fprintf(stdout,"Converting Binary trace file to text\n");
	if(argc == 3) {
		infile = argv[1];
		outfile = argv[2];
		fprintf(stdout,"Reading from %s Writing to %s \n", infile, outfile);
		retval = trace_file_read_open(infile, outfile);
	} else {
			fprintf(stdout, "Call with %s <infile> <outfile> \n", argv[0]);
		retval = -1;
	}
	return retval;
}
