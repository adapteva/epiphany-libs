/*
  The MIT License (MIT)

  Copyright (c) 2013 Adapteva, Inc

  Contributed by Yaniv Sapir <support@adapteva.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* TODO: Does not work with devel driver */

#include <stdio.h>
#include <stdlib.h>

#include "e-hal.h"
#include "epiphany-hal-api-local.h"

#ifndef countof
  #define countof(x)  (sizeof(x)/sizeof(x[0]))
#endif

static char *gen_strings[] = {
	"INVALID!",
	"Parallella-I",
	"UNKNOWN"
};

#define GEN_MAX (countof(gen_strings)-1)

static char *plat_strings_P1[] = {
	"INVALID!",
	"E16, 7Z020, GPIO connectors",
	"E16, 7Z020, no GPIO",
	"E16, 7Z010, GPIO",
	"E16, 7Z010, no GPIO",
	"E64, 7Z020, GPIO",
	"UNKNOWN"
};

#define PLAT_MAX_P1 (countof(plat_strings_P1)-1)

// Type 'A' applies to the first 5 platforms (update as needed)
#define PLAT_GROUP_P1A  5
// Next types should increment from group A, e.g.
#define PLAT_GROUP_P1B  (PLAT_GROUP_P1A + 4)

static char *type_strings_P1A[] = {
	"INVALID!",
	"HDMI enabled, GPIO unused",
	"Headless, GPIO unused",
	"Headless, 24/48 singled-ended GPIOs from EMIO",
	"HDMI enabled, 24/48 singled-ended GPIOs from EMIO",
	"UNKNOWN"
};

#define TYPE_MAX_P1A (countof(type_strings_P1A)-1)

/* Deprecated */
typedef union {
	unsigned int reg;
	struct {
		unsigned int revision:8;
		unsigned int type:8;
		unsigned int platform:8;
		unsigned int generation:8;
	};
} e_syscfg_version_t;

void print_platform_info(e_syscfg_version_t *version)
{
	unsigned int revision = version->revision;
	unsigned int type = version->type;
	unsigned int platform = version->platform;
	unsigned int generation = version->generation;
	char *gen_str, *plat_str, *type_str;

	printf("Epiphany Hardware Version: %02x.%02x.%02x.%02x\n\n",
			generation,
			platform,
			type,
			revision);

	if ((generation & 0x80) != 0 && generation != 0xff) {
		printf("DEBUG/EXPERIMENTAL Version Detected\n");
		generation &= 0x7f;
	}

	if (16 < generation && generation < 21) {
		/* Old-style datecode */
		printf("Old-style datecode\n");
		return;
	}

	if (generation != 1) {
		/* Currently we only know of Parallella-I */
		printf("Unknown generation\n");
		return;
	}

	if (platform > PLAT_GROUP_P1A) {
		printf("Unknown platform\n");
		return;
	}

	gen_str = gen_strings[(generation < GEN_MAX) ?  generation : GEN_MAX];

	plat_str = plat_strings_P1[(platform < PLAT_MAX_P1) ?
		platform : PLAT_MAX_P1];

	type_str = type_strings_P1A[(type < TYPE_MAX_P1A) ?
		type : TYPE_MAX_P1A];

	printf("Generation %d: %s\n", generation, gen_str);
	printf("Platform   %d: %s\n", platform, plat_str);
	printf("Type       %d: %s\n", type, type_str);
	printf("Revision   %d\n", revision);

	printf("\n");
}

int main()
{
	e_syscfg_version_t version;

	if (E_OK != e_init(NULL)) {
		fprintf(stderr, "Epiphany HAL initialization failed\n");
		exit(EXIT_FAILURE);
	}

	/* TODO: Fix */
#if 0
	version.reg = ee_read_esys(E_SYS_VERSION);
#else
	version.reg = 0;
#endif

	print_platform_info(&version);

	e_finalize();

	exit(EXIT_SUCCESS);
}

