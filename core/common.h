/*
 * Copyright 2016-2019, Andrew Lindesay. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Andrew Lindesay, apl@lindesay.co.nz
 */

#ifndef __COMMON_H
#define __COMMON_H

#include <ctype.h>
#include <stdarg.h>
#include <sys/time.h>

#include "constants.h"
#include "types.h"

// ---------------------------------------------------------------
// FILE SYSTEM
// ---------------------------------------------------------------

/*
The caller maintains ownership of this data and should free it.
*/

char *deen_root_dir();
char *deen_data_path(const char *root_dir);
char *deen_index_path(const char *root_dir);

// ---------------------------------------------------------------
// UTILITY
// ---------------------------------------------------------------

time_t deen_seconds_since_epoc();

deen_millis deen_millis_since_epoc();

// ---------------------------------------------------------------
// STRINGS
// ---------------------------------------------------------------

uint8_t *deen_utf8_usascii_equivalent(const uint8_t *c, size_t c_length);

deen_bool deen_utf8_is_usascii_clean(
	const uint8_t *c,
	size_t c_length);

/*
Given a UTF8 string, this function will crop the string to the
desired number of unicode characters.  It is destructive to the
data passed in.  It will return the number of unicode characters
at which it did crop.
*/

size_t deen_utf8_crop_to_unicode_len(
	uint8_t *c,
	size_t c_length,
	size_t unicode_length);

/*
A UTF-8 string may consist of a number of UTF-8 sequences.  This
function will count the number of such sequences in a string
supplied to this function.  This is effectively the number of
characters in the string as opposed to the number of bytes that
the storage of the string takes up.
*/

deen_utf8_sequence_result deen_utf8_sequences_count(
	const uint8_t *c,
	size_t c_length,
	size_t *sequence_count);

/*
Given the UTF-8 sequence at 'c' of length 'len', what is the length of
bytes that will be involved in the sequence.  This function will return 0
if the sequence is invalid.
// see http://en.wikipedia.org/wiki/UTF-8
*/

deen_utf8_sequence_result deen_utf8_sequence_len(
	const uint8_t *c,
	size_t c_length,
	size_t *sequence_length);

/*
Processes the supplied file for words, calling the supplied callback
each time that a word is encountered.  The function should return
'true' to continue with the processing.  Returns true if successful.
*/

deen_bool deen_for_each_word_from_file(
	size_t read_buffer_size,
	int fd,
	deen_bool (*process_callback)(
		const uint8_t *s,
		size_t len,
		off_t ref, // offset after last newline.
		float progress,
		void *context),
	void *context);

/*
For each non-trivial word in the source text, call the callback function.
*/

void deen_for_each_word(
	const uint8_t *s,
	size_t offset,
	deen_bool (*eachword_callback)(const uint8_t *s, size_t offset, size_t len, void *context),
	void *context
);


/*
This function will replace US-ASCII characters with their upper case equivalent
as well as any german accented characters such as a-umlaut, o-umlaut etc..
*/

void deen_to_upper(uint8_t *s);

/*
Does the string 'f' exist in the string 's' at the offet location 'at'?  The
comparison is done case insensitvely.
*/

deen_bool deen_imatches_at(const uint8_t *s, const uint8_t *f, size_t at);

/*
Find the offset into the string 's' at which the string 'f' can be found.  The
search is performed between the offsets 'from' -> 'to'.  The search is case
insensitive.  This function will return DEEN_NOT_FOUND if nothing was able to
be found.
*/

size_t deen_ifind_first(const uint8_t *s, const uint8_t *f, size_t from, size_t to);

/*
Returns true if the text at s is a common word such as 'the', 'der', 'wir' etc...
*/

deen_bool deen_is_common_upper_word(const uint8_t *s, size_t len);

/*
Finds the first instance of the character 'b' within the string 'a'.
*/

uint8_t *deen_strnchr(uint8_t *a, uint8_t b, size_t len);

// ---------------------------------------------------------------
// ENSURED MEMORY ALLOCATION
// ---------------------------------------------------------------

/*
These functions will perform the same role as malloc and realloc, but they
will cause the program to exit if the memory was not able to allocated.
*/

void *deen_emalloc(size_t size);
void *deen_erealloc(void *ptr, size_t size);

// ---------------------------------------------------------------
// LOGGING
// ---------------------------------------------------------------

// private
void deen_vlog(
	const char *prefix,
	const char *file,
	uint32_t line,
	const char *fmt,
	va_list argptr);

// private
void deen_log(
	const char *prefix,
	const char *file,
	uint32_t line,
	const char *fmt,
	...);

void deen_set_trace_enabled(deen_bool flag);
deen_bool deen_is_trace_enabled();

#define DEEN_LOG_TRACE0(FMT) if(deen_is_trace_enabled()) deen_log(DEEN_PREFIX_TRACE,__FILE__,__LINE__,FMT)
#define DEEN_LOG_TRACE1(FMT,ARG1) if(deen_is_trace_enabled()) deen_log(DEEN_PREFIX_TRACE,__FILE__,__LINE__,FMT,ARG1)
#define DEEN_LOG_TRACE2(FMT,ARG1,ARG2) if(deen_is_trace_enabled()) deen_log(DEEN_PREFIX_TRACE,__FILE__,__LINE__,FMT,ARG1,ARG2)
#define DEEN_LOG_TRACE3(FMT,ARG1,ARG2,ARG3) if(deen_is_trace_enabled()) deen_log(DEEN_PREFIX_TRACE,__FILE__,__LINE__,FMT,ARG1,ARG2,ARG3)

#define DEEN_LOG_INFO0(FMT) deen_log(DEEN_PREFIX_INFO,__FILE__,__LINE__,FMT)
#define DEEN_LOG_INFO1(FMT,ARG1) deen_log(DEEN_PREFIX_INFO,__FILE__,__LINE__,FMT,ARG1)
#define DEEN_LOG_INFO2(FMT,ARG1,ARG2) deen_log(DEEN_PREFIX_INFO,__FILE__,__LINE__,FMT,ARG1,ARG2)
#define DEEN_LOG_INFO3(FMT,ARG1,ARG2,ARG3) deen_log(DEEN_PREFIX_INFO,__FILE__,__LINE__,FMT,ARG1,ARG2,ARG3)

#define DEEN_LOG_ERROR0(FMT) deen_log(DEEN_PREFIX_ERROR,__FILE__,__LINE__,FMT)
#define DEEN_LOG_ERROR1(FMT,ARG1) deen_log(DEEN_PREFIX_ERROR,__FILE__,__LINE__,FMT,ARG1)
#define DEEN_LOG_ERROR2(FMT,ARG1,ARG2) deen_log(DEEN_PREFIX_ERROR,__FILE__,__LINE__,FMT,ARG1,ARG2)
#define DEEN_LOG_ERROR3(FMT,ARG1,ARG2,ARG3) deen_log(DEEN_PREFIX_ERROR,__FILE__,__LINE__,FMT,ARG1,ARG2,ARG3)

void deen_log_info(const char *fmt, ...);
void deen_log_error(const char *fmt, ...);
void deen_log_error_and_exit(const char *fmt, ...);

// ---------------------------------------------------------------

#endif /* __COMMON_H */
