/*
 * Micro Communicator for Projection uC
 * a High-Speed Serial communications server
 *
 * XML Configuration Parser
 *
 * Copyright (C) 2018 AngeloGioacchino Del Regno <kholk11@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <expat.h>

#include "ucomm_private.h"

#include <cutils/properties.h>
#include <utils/Log.h>

#define UNUSED __attribute__((unused))

#define LOG_TAG "MicroComm-XMLParser"

#define UCOMM_MAX_FOCTBL_ENTRIES	500

static short xml_depth = 0;
static short parse = -1;
static char* main_node;

static char millimeters[255];
static char focus_steps[255];

struct micro_communicator_focus_params focus_params;

void parseElm(const char *elm, const char **attr)
{
	int i;

	if (strcmp("focus", elm) == 0) {
		for (i = 0; attr[i]; i += 2) {
			if (strcmp("millimeters", attr[i]) == 0)
				strcpy(millimeters, attr[i+1]);
			else if (strcmp("focus_step", attr[i]) == 0)
				strcpy(focus_steps, attr[i+1]);
		}
	}

end:
	return;
}

void startElm(void *data UNUSED, const char *elm, const char **attr)
{
	xml_depth++;

	if (strncmp(main_node, elm, strlen(main_node)) == 0)
		parse = xml_depth;

	if (parse > 0)
		parseElm(elm, attr);
}

void endElm(void *data UNUSED, const char *elm UNUSED)
{
	if ((parse > 0) && (parse == xml_depth))
		parse = -1;

	xml_depth--;
}

void str_handler(void *data, const char *str, int len)
{
	char *buf = malloc(len+1);

	strncpy(buf, str, len);
	buf[len+1] = '\0';

	data = (void*)buf;
}

int parse_ucomm_xml_data(char* filepath, char* node, 
			struct micro_communicator_focus_params *ucomm_focus)
{
	int ret, fd, count, sz;
	char *buf, *mend, *fend;
	struct stat st;
	XML_Parser pa;
	struct micro_communicator_foctbl_entry *tbl_entry = NULL;

	fd = open(filepath, O_RDONLY);
	if (fd < 0) {
		ALOGE("Cannot open configuration file!!!");
		return -ENOENT;
	}

	pa = XML_ParserCreate(NULL);

	stat(filepath, &st);
	sz = st.st_size;

	/* Security check: do NOT parse too big files */
	if (sz > 131072) {
		ALOGE("File is huge. Preventing parse as a security measure.");
		ret = -E2BIG;
		goto secfail;
	}

	buf = malloc(sizeof(char)*sz);

	memset(buf, 0, sz);
	count = read(fd, buf, (sz - 1));
	if (count < 0) {
		ALOGE("Cannot read configuration file!!!");
		ret = -EIO;
		goto end;
	}

	focus_params.table = malloc(UCOMM_MAX_FOCTBL_ENTRIES *
			sizeof(struct micro_communicator_foctbl_entry));
	if (focus_params.table == NULL) {
		ALOGE("Out of memory. Cannot allocate.");
		ret = -ENOMEM;
		goto end;
	}
	focus_params.num_steps = 0;

	XML_SetElementHandler(pa, startElm, endElm);
	XML_SetCharacterDataHandler(pa, str_handler);

	main_node = node;

	if (XML_Parse(pa, buf, strlen(buf), XML_TRUE) == XML_STATUS_ERROR) {
		ALOGE("XML Parse error: %s\n", XML_ErrorString(
						XML_GetErrorCode(pa)));
		ret = -EINVAL;
		goto end;
	}
	ret = 0;

	/* Check values and fill the temporary struct */
	tbl_entry = &focus_params.table[focus_params.num_steps];

	tbl_entry->input_val = (int)strtol(millimeters, &mend, 10);
	tbl_entry->focus_step = (int)strtol(focus_steps, &fend, 10);

	if (tbl_entry->input_val == 0 || tbl_entry->focus_step == 0) {
		ret = -EINVAL;
		free(focus_params.table);
		goto end;
	}

	do {
		focus_params.num_steps++;
		tbl_entry = &focus_params.table[focus_params.num_steps];

		tbl_entry->input_val = (int)strtol(mend, &mend, 10);
		tbl_entry->focus_step = (int)strtol(fend, &fend, 10);

		/* We've reached the end, farewell! */
		if (tbl_entry->input_val == 0 || tbl_entry->focus_step == 0) {
			//focus_params.num_steps--;
			break;
		}
	} while (focus_params.num_steps < UCOMM_MAX_FOCTBL_ENTRIES);

	/* All ok! */
	ucomm_focus->num_steps = focus_params.num_steps;
	ucomm_focus->table = focus_params.table;

end:
	free(buf);
secfail:
	close(fd);
	XML_ParserFree(pa);

	return ret;
}
