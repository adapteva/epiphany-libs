/*
  File: main.cpp

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  See AUTHORS for list of contributors.
  Support e-mail: <support@adapteva.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program (see the file COPYING).  If not, see
  <http://www.gnu.org/licenses/>.
*/



#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <string.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <map>

#include "GdbServer.h"
#include "TargetControlHardware.h"
#include "maddr_defs.h"
#include <e-xml/src/epiphany_xml.h>

// Up to the builder to specify a revision.
#define XDOREVSTR(s) DOREVSTR(s)
#define DOREVSTR(s) #s
#ifdef REVISION
#define REVSTR XDOREVSTR (REVISION)
#else
#define REVSTR XDOREVSTR (undefined)
#endif

using std::cerr;
using std::cout;
using std::stringstream;

unsigned int PORT_BASE_NUM;

char TTY_[1024];


// accesses by all threads
int debug_level = 0;

bool skip_platform_reset = false;

//! Structure to pass data to pthread_create
struct ThreadData
{
  unsigned int  port;
  bool          dontCheckHwAddress;
  bool          haltOnAttach;
  FILE*         ttyOut;
  bool		withTtySupport;
};


//! Put the usage message out on the given stream.

//! Typically std::cout if this is standard help and std::cerr if this is in
//! response to bad arguments.

//! @param[in] s  Stream on which to output the usage.
static void
usage (ostream& s)
{
  s << "Usage:" << endl;
  s << endl;
  s << "e-server -hdf <hdf_file [-p <port-number>] [--show-memory-map]"
    << endl;
  s << "         [-Wpl,<options>] [-Xpl <arg>] [--version] [--h | --help]"
    << endl;
  s << "         [-d <debug-level>] [--tty <terminal>] [--dont-halt-on-attach]" 
    << endl;
  s << "         [-skip-platform-reset]" << endl;
  s << endl;
  s << "Standard program options:" << endl;
  s << endl;
  s << "  -hdf <hdf-file>" << endl;
  s << endl;
  s << "    Specify a platform definition file. This parameter is mandatory and"
    << endl;
  s << "    should be the XML equivalent of the text file specified by the "
    << endl;
  s << "    EPIPHANY_HDF environment variable." << endl;
  s << endl;
  s << "  -p <port-number>" << endl;
  s << endl;
  s << "    Port number on which GDB should connnect. Default is 51000."
    << endl;
  s << endl;
  s << "  --show-memory-map" << endl;
  s << endl;
  s << "    Print out the supported memory map." << endl;
  s << endl;
  s << "  -Wpl <options>" << endl;
  s << endl;
  s << "    Pass comma-separated <options> on to the platform driver."
    << endl;
  s << endl;
  s << "  -Xpl <arg>" << endl;
  s << endl;
  s << "    Pass <arg> on to the platform driver." << endl;
  s << endl;
  s << "  --version" << endl;
  s << endl;
  s << "    Display the version number and copyright information." << endl;
  s << endl;
  s << "  --h | --help" << endl;
  s << endl;
  s << "    Display this help message." << endl;
  s << endl;
  s << "Debug options:" << endl;
  s << endl;
  s << "  -d <debug-level>" << endl;
  s << endl;
  s << "    Run the e-server in debug mode. Default 0 (no debug)." << endl;
  s << endl;
  s << "  --tty <terminal>" << endl;
  s << endl;
  s << "    Redirect the e_printf to terminal with tty name <terminal>."
    << endl;
  s << endl;
  s << "  --dont-halt-on-attach" << endl;
  s << endl;
  s << "    When starting an e-gdb session, the debugger initiates an" << endl;
  s << "    attachment procedure when executing the 'target remote:' command."
    << endl;
  s << "    By default, this procedure includes a reset sequence sent to the"
    << endl;
  s << "    attached core.  Use this option to disable the intrusive attachment"
    << endl;
  s << "    and perform a non-intrusive one that does not change the core's"
    << endl;
  s << "    state.  This allows connection to and monitoring of a core that is"
    << endl;
  s << "    currently running a program." << endl;
  s << endl;
  s << "  --dont-check-hw-address" << endl;
  s << endl;
  s << "    The e-server filters out transactions if the address is invalid"
    << endl;
  s << "    (not included in the device supported memeory map).  Use this"
    << endl;
  s << "    option to disable this protection."
    << endl;
  s << endl;
  s << "Advanced options:" << endl;
  s << endl;
  s << "  -skip-platform-reset" << endl;
  s << endl;
  s << "    Don't make the hardware reset during initialization." << endl;

}	// usage ()


//! Printout version and copyright information
static void
copyright ()
{
  cout << "e-server revision " << REVSTR << " (compiled on "
       << __DATE__ << ")" << endl;
  cout << "Copyright (C) 2010-2013 Adapteva Inc." << endl;
  cout << "The Epiphany XML Parser uses the XML library developed by Michael "
       << "Chourdakis." << endl;
  cout << "Please report bugs to: support-sdk@adapteva.com" << endl;

}	// copyright ()


//! Print out the memory map for the platform

//! @param[in] xml     The XML description of the platform.
//! @param[in] nCores  The number of cores in the target.
static void
showMemoryMap (EpiphanyXML* xml,
	       unsigned int nCores)
{
  extern map < unsigned, pair < unsigned long, unsigned long > >memory_map;
  extern map < unsigned, pair < unsigned long, unsigned long > >register_map;

  xml->PrintPlatform ();
  cout << "Supported registers map: " << endl;
  for (map < unsigned, pair < unsigned long,
	 unsigned long > >::iterator ii = register_map.begin ();
       ii != register_map.end (); ++ii)
    {
      unsigned long startAddr = (*ii).second.first;
      unsigned long endAddr = (*ii).second.second;
      cout << " [" << hex << startAddr << "," << endAddr << dec << "]\n";
    }
  unsigned core_num = 0;
  cout << "Supported memory map: " << endl;
  for (map < unsigned, pair < unsigned long,
	 unsigned long > >::iterator ii = memory_map.begin ();
       ii != memory_map.end (); ++ii)
    {
      unsigned long startAddr = (*ii).second.first;
      unsigned long endAddr = (*ii).second.second;
      
      if (core_num < nCores)
	{
	  cout << "";
	}
      else
	{
	  cout << "External: ";
	}
      cout << " [" << hex << startAddr << "," << endAddr << dec << "]\n";
      core_num++;
    }
}	// showMemoryMap ()


static void *
createGdbServer (void *ptr)
{
  ThreadData *td = (ThreadData *) ptr;

  unsigned int coreNum = td->port - PORT_BASE_NUM;

  TargetControl *tCntrl;
  tCntrl = new TargetControlHardware (coreNum, td->dontCheckHwAddress);

  GdbServer *rspServerP = new GdbServer (td->port, td->haltOnAttach,
					 td->ttyOut, td->withTtySupport);

  //cerr << "Thread id " << pthread_self() << endl << flush;

  rspServerP->rspServer (tCntrl);

  return NULL;
}


int
main (int argc, char *argv[])
{
  char *hdfFile = NULL;
  string platformArgs;
  int mainRetStatus = 0;
  bool doShowMemoryMap = false;

  bool dontCheckHwAddress = false;
  bool haltOnAttach = true;
  FILE *ttyOut = 0;
  bool withTtySupport = false;

  PORT_BASE_NUM = 51000;

  sprintf (TTY_, "tty");

  /////////////////////////////
  // parse command line options
  for (int n = 1; n < argc; n++)
    {
      if (!strcmp (argv[n], "--version"))
	{
	  copyright ();
	  exit (0);
	}
      else if ((!strcmp (argv[n], "-h")) || (!strcmp (argv[n], "--help")))
	{
	  usage (cout);
	  exit (0);
	}
      else if (!strcmp (argv[n], "-hdf"))
        {
	  n += 1;
	  if (n < argc)
	    hdfFile = argv[n];
	  else
	    {
	      usage (cerr);
	      return 3;
	    }
	}
      else if (!strcmp (argv[n], "--dont-check-hw-address"))
	{
	  dontCheckHwAddress = true;
	}
      else if (!strcmp (argv[n], "--dont-halt-on-attach"))
	{
	  haltOnAttach = false;
	}
      else if (!strcmp (argv[n], "-skip-platform-reset"))
	{
	  skip_platform_reset = true;

	}
      else if (!strcmp (argv[n], "-Xpl"))
	{
	  n += 1;
	  if (n < argc)
	      platformArgs += " " + string (argv[n]);
	  else
	    {
	      usage (cerr);
	      exit (3);
	    }
	}
      else if (!strncmp (argv[n], "-Wpl,", strlen ("-Wpl,")))
	{
	  //-Wpl,-abc,-123,-reset_timeout,4
	  stringstream   ss (argv[n]);
	  string         subarg;

	  // Break out the sub-args
	  getline (ss, subarg, ',');	// Skip the -Wpl

	  while (getline (ss, subarg, ','))
	    platformArgs += " " + subarg;
	}
      else if (!strcmp (argv[n], "-p"))
	{
	  n += 1;
	  if (n < argc)
	    {
	      PORT_BASE_NUM = atoi (argv[n]);
	      cout << "Setting base port number to " << PORT_BASE_NUM << "."
		   << endl;
	    }
	  else
	    {
	      usage (cerr);
	      exit (3);
	    }
	}
      else if (!strcmp (argv[n], "--tty"))
	{
	  n += 1;
	  if (n < argc)
	    {
	      withTtySupport = true;
	      strncpy (TTY_, (char *) argv[n], sizeof (TTY_));
	    }
	  else
	    {
	      usage (cerr);
	      exit (3);
	    }
	}
      else if (!strcmp (argv[n], "-d"))
	{
	  n += 1;
	  if (n < argc)
	    {
	      debug_level = atoi (argv[n]);
	      cout << "setting debug level to " << debug_level
		   << endl;
	    }
	  else
	    {
	      usage (cerr);
	      exit (3);
	    }
	}
      else if (!strcmp (argv[n], "--show-memory-map"))
	{
	  doShowMemoryMap = true;
	}
    }

  ////////////////
  // Parse the HDF. Note that contrary to previous advice, the system will
  // barf if you don't set the environment variable. But the -hdf argument is
  // necesary for now, since it specifies the XML equivalent of the
  // environment variable text file.
  // @todo Fix this!
  if (NULL == hdfFile)
    {
      cerr << "Please specify the -hdf argument." << endl << endl;
      usage (cerr);
      exit (3);
    }

  cout << "Using the HDF file: " << hdfFile << endl;
  EpiphanyXML *xml = new EpiphanyXML ((char *) hdfFile);
  if (xml->Parse ())
    {
      delete xml;
      cerr << "Can't parse Epiphany HDF file: " << hdfFile << endl;
      exit (3);
    }

  platform_definition_t *platform = xml->GetPlatform ();
  if (!platform)
    {
      delete xml;
      cerr << "Could not extract platform information from " << hdfFile <<
	endl;
      exit (3);
    }

  // prepare args list to hardware driver library
  platformArgs += string (" ") + string (platform->libinitargs);
  platform->libinitargs = (char *) platformArgs.c_str ();

  //////////////////////////////////////////////////////
  // populate the chip and ext_mem list of memory ranges
  unsigned nCores = TargetControlHardware::initDefaultMemoryMap (platform);

  if (doShowMemoryMap)
    showMemoryMap (xml, nCores);

  // open terminal
  if (withTtySupport)
    {
      ttyOut = fopen (TTY_, "w");
      if (ttyOut == NULL)
	{
	  cerr << "Can't open tty " << TTY_ << endl;
	  exit (2);
	}
    }


  ////////////////////////
  // initialize the device
  TargetControlHardware::initHwPlatform (platform);

  // Create

  if (true)
    {
      // Create independent threads each of which will execute function FIXME:
      // switch to dynamic port creation model n threads for gdb, thread for
      // loader, thread for reset client (Used in Eclipse)
      unsigned portsNum[nCores];

      // loader
///             portsNum[nCores] = PORT_BASE_NUM-1;
      // host reset proxy
///             portsNum[nCores+1] = PORT_BASE_NUM-2;

      for (unsigned i = 0; i < nCores; i++)
	{
	  portsNum[i] = PORT_BASE_NUM + i;
	}

      pthread_t thread[nCores];

      //////////////////////////////////////////////
      // create and execute the thread for the cores
      for (unsigned i = 0; i < nCores; i++)
	{
	  ThreadData td;

	  td.port               = portsNum[i];
	  td.dontCheckHwAddress = dontCheckHwAddress;
	  td.haltOnAttach       = haltOnAttach;
	  td.ttyOut             = ttyOut;
	  td.withTtySupport     = withTtySupport;

	  pthread_create (&(thread[i]), NULL, createGdbServer,
			  (void *) (&td));
	}

      sleep (1);

      /////////////////////////////
      // wait for threads to finish
      /* Wait till threads are complete before main continues. Unless we */
      /* wait we run the risk of executing an exit which will terminate  */
      /* the process and all threads before the threads have completed.  */
      for (unsigned i = 0; i < (nCores); i++)
	{
	  pthread_join ((thread[i]), NULL);
	}

    }

  if (ttyOut)
    {
      fclose (ttyOut);
    }

  delete xml;

  return mainRetStatus;
}


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// End:
