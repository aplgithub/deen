/*
 * Copyright 2016-2019, Andrew Lindesay. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Andrew Lindesay, apl@lindesay.co.nz
 */

#ifndef INSTALL_CPP
#define INSTALL_CPP

#include "install.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <unistd.h>

#include "common.h"
#include "constants.h"
#include "index.h"

/*
This method will open the supplied file and will try to
ascertain if the data is a DING file or not. If not then
the application may warn the user and not proceed with
an install.
 */

#define DEEN_SIZE_CHECK_DING_BUFFER 4 * 1024

/*
 * This is the size of the buffer that will be used when
 * copying the source "ding" data over into the final
 * location for use by the application.
 */

#define DEEN_SIZE_FILE_COPY_BUFFER 4 * 1024

/*
This is the initial size of a buffer used to uppercase text.
*/

#define DEEN_SIZE_UPPER_BUFFER 32


// ---------------------------------------------------------------

/*
This data structure maintains state over the index test and is used
in the callback to point to the tree and the prior progress.
*/

typedef struct deen_index_context deen_index_context;
struct deen_index_context {

	// cancellation management.
	deen_is_cancelled_cb is_cancelled_cb;

	// handle to the index database.
	deen_index_add_context *index_add_context;

	// management of the progress of the indexing.
	float lastprogress;
	void *progress_cb_context;
	deen_install_progress_cb progress_cb;

	// buffer re-used between calls in order to convert text to upper case.
	uint8_t *c_buffer_upper;
	size_t c_buffer_upper_len;

	// tracking the file offset and also the prefixes which are included
	// on that file offset.  The file offset is termed a 'ref'.
	off_t current_ref;
	size_t prefix_count;
	size_t prefix_count_allocated;
	uint8_t **prefixes;

};

// ---------------------------------------------------------------

static const char *deen_cli_state_to_string(enum deen_install_state state) {
	switch(state) {
		case DEEN_INSTALL_STATE_IDLE: return "idle";
		case DEEN_INSTALL_STATE_STARTING: return "starting";
		case DEEN_INSTALL_STATE_INDEXING: return "indexing";
		case DEEN_INSTALL_STATE_COMPLETED: return "completed";
		case DEEN_INSTALL_STATE_ERROR: return "error";
		default: return "???";
	}
}

void deen_log_install_progress(enum deen_install_state state, float progress) {
	switch (state) {
		case DEEN_INSTALL_STATE_INDEXING:
		case DEEN_INSTALL_STATE_STARTING:
		case DEEN_INSTALL_STATE_COMPLETED:
			{
				int percentage = (int) (100.0f * progress);
				DEEN_LOG_INFO2("%12s %3d%%", deen_cli_state_to_string(state), percentage);
			}
			break;

		default:
			DEEN_LOG_INFO1("%s", deen_cli_state_to_string(state));
	}
}

enum deen_install_check_ding_format_check_result deen_install_check_for_ding_format(const char *filename) {

	enum deen_install_check_ding_format_check_result result = DEEN_INSTALL_CHECK_OK;
	int fd = -1;

// see if we have a gzip file; if so then we need to warn the user
// that they need to decompress the file first.

	if (
		DEEN_INSTALL_CHECK_OK == result &&
		strlen(filename) > 3 &&
		0 == strncmp(".gz", &filename[strlen(filename) - 3], 3)) {
		result = DEEN_INSTALL_CHECK_IS_COMPRESSED;
	} else {
		DEEN_LOG_INFO0("candidate file does not appear to be gzip compressed");
	}

// first open the file to be checked.

	if (DEEN_INSTALL_CHECK_OK == result) {
		fd = open(filename, O_RDONLY);
	}

	if (DEEN_INSTALL_CHECK_OK == result && fd < 0) {
		result = DEEN_INSTALL_CHECK_IO_PROBLEM;
	} else {
		DEEN_LOG_INFO0("candidate file was opened successfully");
	}

	// load in some 4k of the file to inspect.

	if (DEEN_INSTALL_CHECK_OK == result) {
		char buffer[DEEN_SIZE_CHECK_DING_BUFFER];

		if (DEEN_SIZE_CHECK_DING_BUFFER != read(fd, buffer, DEEN_SIZE_CHECK_DING_BUFFER)) {
			result = DEEN_INSTALL_CHECK_TOO_SMALL;
		} else {
			DEEN_LOG_INFO1("candidate file; read %d bytes ok", DEEN_SIZE_CHECK_DING_BUFFER);
		}

		if (DEEN_INSTALL_CHECK_OK == result) {

			uint32_t upto = 0;
			deen_bool found_ok_line = DEEN_FALSE;

			while (
				DEEN_INSTALL_CHECK_OK == result &&
				!found_ok_line &&
				(upto < DEEN_SIZE_CHECK_DING_BUFFER)) {

				uint32_t curr = upto;

				while (upto < DEEN_SIZE_CHECK_DING_BUFFER && 0x0a != buffer[upto]) {
					upto++; // find newline.
				}

				if (upto < DEEN_SIZE_CHECK_DING_BUFFER) {
					buffer[upto] = 0;

					if ('#' != buffer[curr] && '\n' != buffer[curr] && 0 != buffer[curr]) {
						buffer[upto] = 0;

						if (NULL != strstr(&buffer[curr], "::")) {
							found_ok_line = DEEN_TRUE;
							DEEN_LOG_INFO1("candidate file; found ok line '%s'", &buffer[curr]);
						} else {
							result = DEEN_INSTALL_CHECK_BAD_FORMAT;
						}
					} else {
						DEEN_LOG_INFO1("candidate file; ignoring comment line '%s'", &buffer[curr]);
					}

					upto++;
				}
			}

			if (DEEN_INSTALL_CHECK_OK == result && !found_ok_line) {
				result = DEEN_INSTALL_CHECK_BAD_FORMAT;
			}
		}
	}

	// close the temporary file handle.

	if (fd >= 0) {
		close(fd);
	}

	return result;
}


static deen_bool deen_exists_fileobject(const char *filename) {
	struct stat s;

	if (-1 == stat(filename, &s)) {
		if (ENOENT == errno) {
			return DEEN_FALSE;
		}
		else {
			DEEN_LOG_INFO1("was unable to 'stat' the file; %s", filename);
			return DEEN_FALSE;
		}
	}

	return DEEN_TRUE;
}


static deen_bool deen_remove_fileobject(const char *filename) {
	deen_bool result = DEEN_FALSE;

	if (deen_exists_fileobject(filename)) {
		if (0 == remove(filename)) {
			DEEN_LOG_INFO1("did remove existing resource; %s", filename);
			result = DEEN_TRUE;
		}
		else {
			DEEN_LOG_ERROR1("failed to remove existing resource; %s", filename);
		}
	}
	else {
		result = DEEN_TRUE;
	}

	return result;
}


static deen_bool deen_remove_fileobject_in_root_dir(const char *deen_root_dir, const char *leafname) {
	char *buffer = (char *) deen_emalloc(strlen(deen_root_dir) + strlen(leafname) + 2);
	uint8_t result;
	sprintf(buffer,"%s%s%s", deen_root_dir, DEEN_FILE_SEP, leafname);
	result = deen_remove_fileobject(buffer);
	return result;
}


/*
 This function will create the deen data directory in the user's home
 folder or in the shared location if there is one.
 */

static deen_bool deen_install_init(const char *deen_root_dir) {

	if (!deen_exists_fileobject(deen_root_dir)) {
		if (0 == 
#ifdef __MINGW32__
					mkdir(deen_root_dir)
#else
					mkdir(deen_root_dir, 0777)
#endif
				) {
			DEEN_LOG_INFO1("did create the deen data directory; %s", deen_root_dir);
		}
		else {
			DEEN_LOG_INFO1("failed to create the deen data directory; %s", deen_root_dir);
			return DEEN_FALSE;
		}
	}

	if (!deen_remove_fileobject_in_root_dir(deen_root_dir, DEEN_LEAF_INDEX)) {
		DEEN_LOG_ERROR0("failed to delete the existing index object");
		return DEEN_FALSE;
	}

	if (!deen_remove_fileobject_in_root_dir(deen_root_dir, DEEN_LEAF_DING_DATA)) {
			DEEN_LOG_ERROR0("failed to delete the existing data object");
		return DEEN_FALSE;
	}

	return DEEN_TRUE;
}


// ---------------------------------------------------------------

/*
Used in order to compare two prefixes in order to be able to sort
them.
*/

static int deen_index_prefix_compare(const void *a, const void *b) {
	uint8_t **a_ptr = (uint8_t **) a;
	uint8_t **b_ptr = (uint8_t **) b;
	return strcmp((const char *) (a_ptr[0]), (const char *) (b_ptr[0]));
}

/*
Adds the prefix to the context even if it is already present.  This function
will add memory to the context if necessary and will ensure that the list of
prefixes is sorted.
*/

static void deen_index_add_prefix_to_context(
	deen_index_context *context,
	uint8_t *s,
	size_t len) {

	if (context->prefix_count == context->prefix_count_allocated) {
		context->prefix_count_allocated++;
		context->prefixes = (uint8_t **) deen_erealloc(
			context->prefixes,
			sizeof(uint8_t **) * context->prefix_count_allocated);
		context->prefixes[context->prefix_count_allocated-1] = (uint8_t *) deen_emalloc(
			sizeof(uint8_t) * (DEEN_INDEXING_DEPTH * 4 + 1));
	}

	memcpy(context->prefixes[context->prefix_count], s, len);
	(context->prefixes[context->prefix_count])[len] = 0;
	context->prefix_count++;

	// sorting
	qsort(
		context->prefixes, context->prefix_count,
		sizeof(uint8_t *), &deen_index_prefix_compare);
}

/*
Checks to see if the prefix is already in place.  If it is in place,
then it will carry on.  If it is not already in place then it will
add it in.
*/

static void deen_index_add_prefix_to_context_if_not_present(
	deen_index_context *context,
	uint8_t *s,
	size_t len) {

	if (0 == context->prefix_count || NULL == bsearch(
		&s,
		context->prefixes,
		context->prefix_count,
		sizeof(uint8_t *),
		&deen_index_prefix_compare)) {

		deen_index_add_prefix_to_context(context,s,len);
	}
}


/*
This is by-passing the regular logging system in order to more efficiently
output this data.
*/

static void deen_index_flush_context_prefixes_to_index_trace_log(deen_index_context *context) {

	if (deen_is_trace_enabled()) {
		size_t i;

		fputs(DEEN_PREFIX_TRACE, stdout);
		fprintf(stdout, " %8lu <-- { ", (unsigned long) context->current_ref);

		for (i = 0; i < context->prefix_count; i++) {
			if (0!=i) {
				fputs(", ",stdout);
			}

			fputs((char *) context->prefixes[i], stdout);
		}

		fputs(" }\n", stdout);
	}
}


static void deen_index_flush_context_prefixes_to_index(
	deen_index_context *context) {

	if (0 != context->prefix_count) {
		deen_index_flush_context_prefixes_to_index_trace_log(context);

		deen_index_add(
			context->index_add_context,
			context->current_ref,
			context->prefixes,
			context->prefix_count);

		context->prefix_count = 0;
	}

}


/*
This call-back method is hit each time a word is found to be indexed
It keeps track of the tree into which the index is being written and
the last percentage of the progress through a struct.
*/

static deen_bool deen_index_callback(
	const uint8_t *s,
	size_t len,
	off_t ref,
	float progress,
	void *context) {

	deen_bool result = DEEN_TRUE;

	deen_index_context *context2 = (deen_index_context *) context;

	if (context2->current_ref != ref) {
		deen_index_flush_context_prefixes_to_index(context2);
		context2->current_ref = ref;

		// handle the progress callback.

		{
			uint8_t last_percent = (uint8_t) (context2->lastprogress * 100.0);
			uint8_t percent = (uint8_t) (progress * 100.0);

			if (percent != last_percent) {
				context2->progress_cb(context2->progress_cb_context,
					DEEN_INSTALL_STATE_INDEXING, progress);
				context2->lastprogress = progress;
			}
		}

	}

	if (len >= DEEN_INDEXING_MIN) {
		if (context2->is_cancelled_cb(context2->progress_cb_context)) {
			result = DEEN_FALSE; // stop processing
		}
		else {

			// ensure the upper-casing buffer is actually large enough.

			if (0==context2->c_buffer_upper_len) {
				context2->c_buffer_upper_len = DEEN_SIZE_UPPER_BUFFER;
				context2->c_buffer_upper = (uint8_t *) deen_emalloc(sizeof(uint8_t) * context2->c_buffer_upper_len);
			}

			if (context2->c_buffer_upper_len <= len) {
				context2->c_buffer_upper_len = len + 1;
				context2->c_buffer_upper = (uint8_t *) deen_emalloc(sizeof(uint8_t) * context2->c_buffer_upper_len);
			}

			memcpy(context2->c_buffer_upper, s, len);
			context2->c_buffer_upper[len] = 0;

			deen_to_upper(context2->c_buffer_upper);

			if (!deen_is_common_upper_word(context2->c_buffer_upper, len)) {

				// create the prefix at the right length.

				size_t unicode_length = deen_utf8_crop_to_unicode_len(context2->c_buffer_upper, len, DEEN_INDEXING_DEPTH);

				if (unicode_length >= DEEN_INDEXING_MIN) {
					deen_index_add_prefix_to_context_if_not_present(
						context2,
						context2->c_buffer_upper,
						strlen((char *) context2->c_buffer_upper));
				}
			}
		}
	}

	return result;
}


deen_bool deen_noop_is_cancelled_cb(void *context) {
	return DEEN_FALSE;
}

deen_bool deen_noop_install_progress_cb(
	void *context, enum deen_install_state state, float progress) {
	return DEEN_TRUE; // keep going
}


#define DEEN_INSTALL_RAISE_ERROR progress_cb(process_cb_context, DEEN_INSTALL_STATE_ERROR, 0.0f); is_error=DEEN_TRUE;


deen_bool deen_install_from_path(
	const char *deen_root_dir,
	const char *ding_filename,
	void *process_cb_context,
	deen_install_progress_cb progress_cb,
	deen_is_cancelled_cb is_cancelled_cb) {

	if (NULL == progress_cb) {
		progress_cb = deen_noop_install_progress_cb;
	}

	if (NULL == is_cancelled_cb) {
		is_cancelled_cb = deen_noop_is_cancelled_cb;
	}

	int fd_data;
	sqlite3 *db = NULL;
	deen_bool is_error = DEEN_FALSE;
	char *data_path = deen_data_path(deen_root_dir);
	char *index_path = deen_index_path(deen_root_dir);

	progress_cb(process_cb_context, DEEN_INSTALL_STATE_STARTING, 0.0f);

	deen_install_init(deen_root_dir);

	// first thing is to copy the file over to the new location.

	if (!is_error && !is_cancelled_cb(process_cb_context)) {
		int fd_src_data = open(ding_filename,O_RDONLY
#ifdef __MINGW32__
			|O_BINARY
#endif
		);

		if (-1 == fd_src_data) {
			DEEN_LOG_INFO1("unable to open the input data file %s",ding_filename);
			DEEN_INSTALL_RAISE_ERROR
		}
		else {
			int fd_dest_data;

			DEEN_LOG_INFO1("source opened for copy to install location; %s",ding_filename);

			fd_dest_data = open(
				data_path,
				O_RDWR|O_CREAT|O_TRUNC
#ifdef __MINGW32__
				|O_BINARY
#endif
				,
				S_IRUSR
#ifndef __MINGW32__
				|S_IRGRP|S_IROTH
#endif
							);

			if (-1==fd_dest_data) {
				DEEN_LOG_INFO1("unable to open the output data file %s",data_path);
				DEEN_INSTALL_RAISE_ERROR
			}
			else {
				void *buffer;
				ssize_t bytes_read;

				DEEN_LOG_INFO1("destination opened for copy to install location; %s",data_path);

				buffer = (void *) deen_emalloc(DEEN_SIZE_FILE_COPY_BUFFER);

				while (!is_error && (bytes_read = read(fd_src_data,buffer,DEEN_SIZE_FILE_COPY_BUFFER)) > 0) {
					if (write(fd_dest_data, buffer, bytes_read) < bytes_read) {
						DEEN_LOG_ERROR2("unable to copy the data from %s --> %s", ding_filename, data_path);
						DEEN_INSTALL_RAISE_ERROR
					}
				}

				DEEN_LOG_INFO0("completed copy");

				free(buffer);

				close(fd_dest_data);
			}

			close(fd_src_data);
		}
	}

	// create the target sqllite database.

	if (!is_error && !is_cancelled_cb(process_cb_context)) {
		if (SQLITE_OK != sqlite3_open_v2(
			index_path,
			&db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
			NULL)) {

			DEEN_LOG_ERROR2("unable to open the sqllite3 database; %s (%s)", index_path, sqlite3_errmsg(db));
			DEEN_INSTALL_RAISE_ERROR
		}
	}

	if (!is_error && !is_cancelled_cb(process_cb_context)) {
		deen_index_init(db);
		DEEN_LOG_TRACE0("did initialize the index database");
	}

	fd_data = -1;

	if (!is_error && !is_cancelled_cb(process_cb_context)) {
		fd_data = open(data_path, O_RDONLY);

		if ((-1==fd_data) || DEEN_CAUSE_ERROR_IN_INSTALL) {
			DEEN_LOG_ERROR1("unable to open the input data file %s",data_path);
			DEEN_INSTALL_RAISE_ERROR
		}
		else {
			DEEN_LOG_INFO1("opened input data file %s",data_path);
		}
	}

	if (!is_error && !is_cancelled_cb(process_cb_context)) {
		time_t secs_before;
		deen_index_context index_context;

		index_context.index_add_context = deen_index_add_context_create(db);
		index_context.lastprogress = -1.0f;
		index_context.progress_cb_context = process_cb_context;
		index_context.progress_cb = progress_cb;
		index_context.is_cancelled_cb = is_cancelled_cb;
		index_context.c_buffer_upper = NULL;
		index_context.c_buffer_upper_len = 0;
		index_context.current_ref = 0;
		index_context.prefix_count = 0;
		index_context.prefix_count_allocated = 0;
		index_context.prefixes = NULL;

		secs_before = deen_seconds_since_epoc();

		deen_transaction_begin(db);

		if (!deen_for_each_word_from_file(
			DEEN_BUFFER_SIZE_EACH_WORD_FROM_FILE,
			fd_data,
			&deen_index_callback,
			&index_context)) {
			DEEN_LOG_ERROR1("failure to process the file %s", data_path);
			DEEN_INSTALL_RAISE_ERROR
		}

		deen_transaction_commit(db);

		// print out the performance of the indexing with respect to database
		// activity

#ifdef DEBUG
		if (!is_error) {
			DEEN_LOG_INFO1("db activity; find existing prefixes = %llu ms", index_context.index_add_context->find_existing_prefixes_millis);
			DEEN_LOG_INFO1("db activity; add missing prefixes = %llu ms", index_context.index_add_context->add_missing_prefixes_millis);
			DEEN_LOG_INFO1("db activity; add refs = %llu ms", index_context.index_add_context->add_refs_millis);
		}
#endif

		// flush any indexes to the database.

		deen_index_flush_context_prefixes_to_index(&index_context);

		if (!is_error) {
			DEEN_LOG_INFO1("indexed in %u seconds", deen_seconds_since_epoc() - secs_before);
		}

		if (NULL != index_context.index_add_context) {
			deen_index_add_context_free(index_context.index_add_context);
		}

		// release memory that might have been used in the indexing process
		// as stored in the context.

		if (NULL != index_context.c_buffer_upper) {
			free((void *) index_context.c_buffer_upper);
		}

		if (NULL != index_context.prefixes) {
			size_t i;

			for (i=0;i<index_context.prefix_count_allocated;i++) {
				free((void *) index_context.prefixes[i]);
			}

			free((void *) index_context.prefixes);
		}
	}

	if (-1 != fd_data) {
		close(fd_data);
		DEEN_LOG_INFO1("closed input file; %s",data_path);
	}

	if (NULL != db) {
		sqlite3_close_v2(db);
		DEEN_LOG_INFO1("closed index database; %s",index_path);
	}

	// if the install process did not work out then we need to
	// delete the stored data as well as any partially written
	// index.

	if (is_error || is_cancelled_cb(process_cb_context)) {
		DEEN_LOG_ERROR0("indexing not completed -> clean up files");
		deen_remove_fileobject(data_path);
		deen_remove_fileobject(index_path);
	}

	free((void *) data_path);
	free((void *) index_path);

	if (!is_error) {
		progress_cb(process_cb_context, DEEN_INSTALL_STATE_COMPLETED, 1.0f);
	} else {
		if (is_cancelled_cb(process_cb_context)) {
			progress_cb(process_cb_context, DEEN_INSTALL_STATE_IDLE, 0.0f);
		}
	}

	return !is_error;
}

deen_bool deen_is_installed(const char *deen_root_dir) {
	char *data_path = (char *) deen_emalloc(strlen(deen_root_dir) + strlen(DEEN_LEAF_DING_DATA) + 2);
	sprintf(data_path, "%s%s%s", deen_root_dir, DEEN_FILE_SEP, DEEN_LEAF_DING_DATA);
	return deen_exists_fileobject(data_path);
}

#endif /* INSTALL_CPP */
