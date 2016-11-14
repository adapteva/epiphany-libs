// Main program for the Epiphany GDB Remote Serial Protocol Server

// This file is part of the Epiphany Software Development Kit.

// Copyright (C) 2013-2014 Adapteva, Inc.
// Copyright (C) 2016 Pedro Alves

// Contributor: Oleg Raikhman <support@adapteva.com>
// Contributor: Yaniv Sapir <support@adapteva.com>
// Contributor: Jeremy Bennett <jeremy.bennett@embecosm.com>
// Contributor: Pedro Alves <pedro@palves.net>

// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.

// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.

// You should have received a copy of the GNU General Public License along
// with this program (see the file COPYING).  If not, see
// <http://www.gnu.org/licenses/>.


#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <unistd.h>

#include <libgen.h>

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <map>

#include "GdbServer.h"
#include "ServerInfo.h"
#include "TargetControlHardware.h"
#include "epiphany_xml.h"
#include "epiphany-hal-data.h"


// Up to the builder to specify a revision.
#define XDOREVSTR(s) DOREVSTR(s)
#define DOREVSTR(s) #s
#ifdef REVISION
#define REVSTR XDOREVSTR (REVISION)
#else
#define REVSTR XDOREVSTR (undefined)
#endif


// The below paths conform to the Epiphany SDK Directory Structure.
// For more information, see the Epiphany SDK Reference Manual.

//! @todo: Define in epiphany-hal-data.h ?
#ifndef E_DEFAULT_EPIPHANY_HOME
#define E_DEFAULT_EPIPHANY_HOME "/opt/adapteva/esdk"
#endif
#ifndef E_XML_DEFAULT_REL_PATH
#define E_XML_DEFAULT_REL_PATH "bsps/current/platform.xml"
#endif

// e-server binary EPIPHANY_HOME relative path
#define E_HOME_REL_PATH "../../.."


using std::cerr;
using std::cout;
using std::dec;
using std::hex;
using std::map;
using std::ostream;
using std::pair;
using std::stringstream;


//! Put the summary usage message out on the given stream

//! Typically std::cout if this is standard help and std::cerr if this is in
//! response to bad arguments.

//! @param[in] s  Stream on which to output the summyar usage.
static void
usage_summary (ostream& s)
{
  s << "Usage:" << endl;
  s << endl;
  s << "e-server -hdf <hdf_file [-p <port-number>] [--show-memory-map]"
    << endl;
  s << "         [--tty <terminal>] [--version] [--h | --help]"
    << endl;
  s << "         [-d <debug-level>] [--hal-debug <level> [--check-hw-address]"
    << endl;
  s << "         [--dont-halt-on-attach] "
    << endl;
  s << "         [-Wpl,<options>] [-Xpl <arg>]"
    << endl;

}	// usage_summary ()


//! Put the full usage message out on the given stream.

//! Typically std::cout if this is standard help and std::cerr if this is in
//! response to bad arguments.

//! @param[in] s  Stream on which to output the usage.
static void
usage_full (ostream& s)
{
  usage_summary (s);

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
  s << "  --tty <terminal>" << endl;
  s << endl;
  s << "    Redirect the e_printf to terminal with tty name <terminal>."
    << endl;
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
  s << "  -d stop-resume" << endl;
  s << "  -d trap-and-rsp-con" << endl;
  s << "  -d stop-resume-detail" << endl;
  s << "  -d target-wr" << endl;
  s << "  -d ctrl-c-wait" << endl;
  s << "  -d tran-detail" << endl;
  s << "  -d hw-detail" << endl;
  s << "  -d timing" << endl;
  s << endl;
  s << "    Enable specified class of debug messages. Use multiple times for"
    << endl;
  s << "    multiple classes of debug message. Default no debug." << endl;
  s << endl;
  s << "  --hal-debug <level>" << endl;
  s << endl;
  s << "    Enable HAL debug level <level>. Default 0 (no debug). Permitted"
    << endl;
  s << "    values are 0 to 4, larger values will be treated as 4 with a"
    << endl;
  s << "    warning." << endl;
  s << endl;
  s << "Advanced options:" << endl;
  s << endl;
  s << "  --check-hw-address" << endl;
  s << endl;
  s << "    If set, the e-server will fail with an error if given an address"
    << endl;
  s << "    that does not correspond to a valid core or external memory. "
    << endl;
  s << "    Otherwise all addresses are accepted without checking. Note that"
    << endl;
  s << "    selecting this option carries some performance penalty."
    << endl;
  s << endl;
  s << "  --dont-halt-on-attach" << endl;
  s << endl;
  s << "    When starting an e-gdb session, the debugger initiates an" << endl;
  s << "    attachment procedure when executing the 'target remote:' command."
    << endl;
  s << "    Use this option to disable the intrusive attachment"
    << endl;
  s << "    and perform a non-intrusive one that does not change the core's"
    << endl;
  s << "    state.  This allows connection to and monitoring of a core that is"
    << endl;
  s << "    currently running a program." << endl;
  s << endl;

  s << "  --multiprocess" << endl;
  s << endl;
  s << "    When starting an e-gdb session, the debugger automatically" << endl;
  s << "    attaches to the idle process." << endl;
  s << "    Use this option to disable this automatic attachment."  << endl;
  s << endl;

  s << "  -Wpl <options>" << endl;
  s << endl;
  s << "    Pass comma-separated <options> on to the platform driver."
    << endl;
  s << endl;
  s << "  -Xpl <arg>" << endl;
  s << endl;
  s << "    Pass <arg> on to the platform driver." << endl;

}	// usage_full ()


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


//! Initialize the hardware platform

//! @todo Contrary to previous advice, the system will
//! barf if you don't set the environment variable. But the -hdf argument is
//! necesary for now, since it specifies the XML equivalent of the
//! environment variable text file. We need to fix this!

//! @param[in] si            Server information with HDF file name and command
//!                          line flags.
//! @param[in] platformArgs  Additional args (if any) to be passed to the
//!                          platform.
static TargetControl*
initPlatform (ServerInfo *si,
	      string      platformArgs)
{
  const char * hdfFile = si->hdfFile ();
  if (NULL != hdfFile)
    cout << "Using the HDF file: " << hdfFile << endl;
  else
    {
      cerr << "Please specify the -hdf argument." << endl << endl;
      usage_summary (cerr);
      exit (EXIT_FAILURE);
    }

  EpiphanyXML *xml = new EpiphanyXML ((char *) hdfFile);
  if (xml->Parse ())
    {
      cerr << "Can't parse Epiphany HDF file: " << hdfFile << "." << endl;
      delete xml;
      exit (EXIT_FAILURE);
    }

  platform_definition_t *platform = xml->GetPlatform ();
  if (!platform)
    {
      cerr << "Can't extract platform info from " << hdfFile << "." << endl;
      delete xml;
      exit (EXIT_FAILURE);
    }

  // prepare args list to hardware driver library
  string initArgs = platformArgs + " " + platform->libinitargs;
  platform->libinitargs = (char *) initArgs.c_str ();

  // Set up the hardware
  TargetControlHardware* tCntrl = new TargetControlHardware (si);

  // populate the chip and ext_mem list of memory ranges and optionally show
  // it.
  tCntrl->initMaps (platform);
  if (si->showMemoryMap ())
    {
      xml->PrintPlatform ();
      cout << endl;
      tCntrl->showMaps ();
    }

  // initialize the device
  tCntrl->initHwPlatform (platform);

  //! @todo. The XML is somehow corrupted. If we delete this, then the stack
  //!        will be corrupted. Needs some valgrind work.
  // delete xml;
  return tCntrl;

}	// initPlatform ()


//! Get absolute path of default XML HDF file

//! @todo Getting the path to the running binary is platform dependent so
//! we need to implement that every platforms we want to support.

//! @param[out] path         Absolute path to default XML HDF file.
static void
getDefaultHdfFile (string& path)
{
  char *programAbsPath, *xmlAbsPath;
  string programDir, xmlRelPath;

#ifdef __linux__

  // Get absolute path to program
  programAbsPath = realpath ("/proc/self/exe", NULL);
  if (NULL == programAbsPath)
    goto fallback_path;

  programDir = dirname (programAbsPath);
  free (programAbsPath);

  xmlRelPath = programDir + "/" E_HOME_REL_PATH + "/" + E_XML_DEFAULT_REL_PATH;

  // Convert relative XML HDF relative path to absolute path
  xmlAbsPath = realpath (xmlRelPath.c_str(), NULL);
  if (NULL == xmlAbsPath)
    goto fallback_path;

  // Copy path
  path = xmlAbsPath;

  free (xmlAbsPath);

  return;

#else
#warning "Platform not supported. Will resort to absolute path. "\
	 "Please implement me."
      cerr << "WARNING: getDefaultHdfFile(): Platform not supported." << endl;
#endif

fallback_path:
  cerr << "WARNING: getDefaultHdfFile(): Falling back to default path."
       << endl;
  path = string("") + E_DEFAULT_EPIPHANY_HOME + "/" + E_XML_DEFAULT_REL_PATH;

}	// getDefaultHdf ()



int
main (int argc, char *argv[])
{
  ServerInfo *si = new ServerInfo;
  string platformArgs;
  string defaultHdfFile;

  /////////////////////////////
  // parse command line options
  for (int n = 1; n < argc; n++)
    {
      if (!strcmp (argv[n], "--version"))
	{
	  copyright ();
	  exit (EXIT_SUCCESS);
	}
      else if ((!strcmp (argv[n], "-h")) || (!strcmp (argv[n], "--help")))
	{
	  usage_full (cout);
	  exit (EXIT_SUCCESS);
	}
      else if (!strcmp (argv[n], "-hdf"))
        {
	  n += 1;
	  if (n < argc)
	    si->hdfFile (argv[n]);
	  else
	    {
	      usage_summary (cerr);
	      return 3;
	    }
	}
      else if (!strcmp (argv[n], "--check-hw-address"))
	si->checkHwAddr (true);
      else if (!strcmp (argv[n], "--dont-halt-on-attach"))
	si->haltOnAttach (false);
      else if (!strcmp (argv[n], "--show-memory-map"))
	si->showMemoryMap (true);
      else if (!strcmp (argv[n], "--hal-debug"))
	{
	  n += 1;
	  if (n < argc)
	    si->halDebug ((e_hal_diag_t) atoi (argv[n]));
	  else
	    {
	      usage_summary (cerr);
	      exit (EXIT_FAILURE);
	    }
	}
      else if (!strcmp (argv[n], "-Xpl"))
	{
	  n += 1;
	  if (n < argc)
	      platformArgs += " " + string (argv[n]);
	  else
	    {
	      usage_summary (cerr);
	      exit (EXIT_FAILURE);
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
	      si->port ((unsigned int) atoi (argv[n]));
	      if (si->validPort ())
		cout << "Port number " << si->port () << "." << endl;
	      else
		{
		  cerr << "ERROR: Invalid port number: " << si->port () << "."
		       << endl;
		  exit (EXIT_FAILURE);
		}
	    }
	  else
	    {
	      usage_summary (cerr);
	      exit (EXIT_FAILURE);
	    }
	}
      else if (!strcmp (argv[n], "--tty"))
	{
	  n += 1;
	  if (n < argc)
	    {
	      si->ttyOut (fopen (argv[n], "w"));
	      if (NULL == si->ttyOut ())
		{
		  cerr << "ERRORL: Can't open tty " << argv[n] << endl;
		  exit (EXIT_FAILURE);
		}
	    }
	  else
	    {
	      usage_summary (cerr);
	      exit (EXIT_FAILURE);
	    }
	}
      else if (!strcmp (argv[n], "--multiprocess"))
	{
	  si->multiProcess (true);
	}
      else if (!strcmp (argv[n], "-d"))
	{
	  n += 1;
	  if (n < argc)
	    {
	      if (0 == strcasecmp (argv[n], "stop-resume"))
		si->debugStopResume (true);
	      else if (0 == strcasecmp (argv[n], "trap-and-rsp-con"))
		si->debugTrapAndRspCon (true);
	      else if (0 == strcasecmp (argv[n], "stop-resume-detail"))
		{
		  si->debugStopResume (true);		// Implied
		  si->debugStopResumeDetail (true);
		}
	      else if (0 == strcasecmp (argv[n], "target-wr"))
		si->debugTargetWr (true);
	      else if (0 == strcasecmp (argv[n], "ctrl-c-wait"))
		si->debugCtrlCWait (true);
	      else if (0 == strcasecmp (argv[n], "tran-detail"))
		si->debugTranDetail (true);
	      else if (0 == strcasecmp (argv[n], "hw-detail"))
		si->debugHwDetail (true);
	      else if (0 == strcasecmp (argv[n], "timing"))
		si->debugTiming (true);
	      else
		{
		  cerr << "WARNING: Unrecognized debug flag " << argv[n]
		       << ": ignored." << endl;
		}
	    }
	  else
	    {
	      usage_summary (cerr);
	      exit (EXIT_FAILURE);
	    }
	}
      else
	{
	  cerr << "ERROR: Unrecognized argument: " << argv[n] << "." << endl;
	  usage_summary (cerr);
	  exit (EXIT_FAILURE);
	}
    }

  // If user did not specify --hdf, use default.
  if (NULL == si->hdfFile())
    {
      getDefaultHdfFile (defaultHdfFile);
      if (!defaultHdfFile.empty())
	si->hdfFile (defaultHdfFile.c_str());
    }

  // Create the single port listening for GDB RSP packets.
  // @todo We may need a separate thread to listen for BREAK.
  GdbServer* rspServerP = new GdbServer (si);

  // @todo We really need this for just one port.
  TargetControl* tCntrl = initPlatform (si, platformArgs);
  rspServerP->rspServer (tCntrl);

  // Tidy up
  if (NULL != si->ttyOut ())
    fclose (si->ttyOut ());

  delete tCntrl;
  delete si;

  exit (EXIT_SUCCESS);

}	// main ()


// Local Variables:
// mode: C++
// c-file-style: "gnu"
// show-trailing-whitespace: t
// End:
