/*
 * Copyright 2016, Andrew Lindesay. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Andrew Lindesay, apl@lindesay.co.nz
 */

#include <stdlib.h>
#include <string.h>

#include "keyword.h"
#include "common.h"
#include "constants.h"

static size_t deen_keywords_sequence_count_or_exit(const uint8_t *c, size_t len) {
	size_t sequence_count;

	switch (deen_utf8_sequences_count(c, len, &sequence_count)) {

		case DEEN_SEQUENCE_OK:
			return sequence_count;
			break;

		case DEEN_BAD_SEQUENCE:
			deen_log_error_and_exit("encountered bad utf-8 sequence");
			break;

		case DEEN_INCOMPLETE_SEQUENCE:
			deen_log_error_and_exit("encountered incomplete utf-8 sequence");
			break;

	}

	return 0;
}

static int deen_keywords_compare_length(const void *k1, const void *k2) {
	const uint8_t *c1 = ((uint8_t **) k1)[0];
	const uint8_t *c2 = ((uint8_t **) k2)[0];
	size_t cl1 = strlen((const char *) c1);
	size_t cl2 = strlen((const char *) c2);
	size_t sequence_count_1 = deen_keywords_sequence_count_or_exit(c1,cl1);
	size_t sequence_count_2 = deen_keywords_sequence_count_or_exit(c2,cl2);

	if (sequence_count_1==sequence_count_2) {

	// now do a lexographical comparison to provide a total
	// ordering.

		return strcmp((const char *) c1, (const char *) c2);
	}

	if (sequence_count_1 < sequence_count_2) {
		return 1;
	}

    return -1;
}

deen_keywords *deen_keywords_create() {
	deen_keywords *keywords = (deen_keywords *) deen_emalloc(sizeof(deen_keywords));
	keywords->count = 0;
	return keywords;
}

void deen_keywords_free(deen_keywords *keywords) {
	for (uint32_t i=0;i<keywords->count;i++) {
		free((void *) keywords->keywords[i]);
	}

	free((void *) keywords->keywords);
	free((void *) keywords);
}


/**
 * This function will check to see if the supplied prefix already exists within
 * the supplied keywords.  There is no point in adding the keyword that is
 * this prefix if it is already there.
 */

static deen_bool deen_keywords_has_prefix(
	deen_keywords *keywords,
	uint8_t *prefix,
	size_t prefix_len) {

	for (uint32_t i=0;i<keywords->count;i++) {
		if (0 == memcmp(keywords->keywords[i], prefix, prefix_len)) {
	    	return DEEN_TRUE;
		}
    }

    return DEEN_FALSE;
}


void deen_keywords_add_from_string(deen_keywords *keywords, uint8_t *input) {

	deen_to_upper(input);

	size_t input_length = strlen((char *)input);
	size_t i = 0;

	while (i < input_length) {
		size_t start;

		while ((i<input_length) && isspace(input[i])) i++;
		start = i;
		while ((i<input_length) && !isspace(input[i])) i++;

		if (i>start &&
			!deen_is_common_upper_word(&input[start], i-start) &&
			!deen_keywords_has_prefix(keywords, &input[start], i-start)) {

			size_t keyword_len = i-start;
			size_t keyword_size = sizeof(uint8_t) * (keyword_len + 1);

			if (0==keywords->count) {
				keywords->count = 1;
				keywords->keywords = (uint8_t **) deen_emalloc(sizeof(uint8_t *));
				keywords->keywords[0] = (uint8_t *) deen_emalloc(keyword_size);
			}
			else {
				keywords->count++;
				keywords->keywords = (uint8_t **) deen_erealloc(
					keywords->keywords,
					sizeof(uint8_t *) * keywords->count);
				keywords->keywords[keywords->count-1] = (uint8_t *) deen_emalloc(keyword_size);
			}

			keywords->keywords[keywords->count-1][keyword_len] = 0;
			memcpy(keywords->keywords[keywords->count-1], &input[start], keyword_len);
		}
    }

	// we need to go through the keywords now and sort them by size;
	// doing this makes some latter algorithms more easy and more
	// efficient.

    qsort(
	    keywords->keywords,
	    keywords->count,
	    sizeof(uint8_t *),
	    &deen_keywords_compare_length);
}

size_t deen_keywords_longest_keyword(deen_keywords *keywords) {
	size_t longest = 0;

	for (uint32_t i=0;i<keywords->count;i++) {
		size_t len = strlen((const char *) keywords->keywords[i]);

		if (len > longest) {
			longest = len;
		}
	}

	return longest;
}


deen_bool deen_keywords_all_present(deen_keywords *keywords, const uint8_t *input) {
	size_t input_len = strlen((char *) input);

    for (uint32_t i=0;i < keywords->count; i++) {
		if (DEEN_NOT_FOUND == deen_ifind_first(input, keywords->keywords[i], 0, input_len)) {
		    return DEEN_FALSE;
		}
    }

    return DEEN_TRUE;
}


static deen_bool deen_keywords_single_substitute_german_usascii_abbreviations(
	uint8_t *str,
	const uint8_t *search,
	const uint8_t *replace) {

	deen_bool result = DEEN_FALSE;

	while (NULL != (str = (uint8_t *) strstr(
		(const char *) str,
		(const char *) search))) {
		result = DEEN_TRUE;
		memcpy(str, replace, 2);
		str++;
	}

	return result;
}


static deen_bool deen_keywords_substitute_german_usascii_abbreviations(uint8_t *value) {
	static uint8_t s_Ee[] = { 'E','E',0 };
	static uint8_t s_Ue[] = { 'U','E',0 };
	static uint8_t s_Oe[] = { 'O','E',0 };
	static uint8_t s_Ae[] = { 'A','E',0 };
	static uint8_t s_Ie[] = { 'I','E',0 };
	static uint8_t s_ss[] = { 'S','S',0 };

	static uint8_t r_Ee[] = { 0xc3,0x8b,0 };
	static uint8_t r_Ue[] = { 0xc3,0x9c,0 };
	static uint8_t r_Oe[] = { 0xc3,0x96,0 };
	static uint8_t r_Ae[] = { 0xc3,0x84,0 };
	static uint8_t r_Ie[] = { 0xc3,0x8f,0 };
	static uint8_t r_ss[] = { 0xc3,0x9f,0 };

	deen_bool adjusted = DEEN_FALSE;

	adjusted = adjusted | deen_keywords_single_substitute_german_usascii_abbreviations(value,s_Ee,r_Ee);
	adjusted = adjusted | deen_keywords_single_substitute_german_usascii_abbreviations(value,s_Ue,r_Ue);
	adjusted = adjusted | deen_keywords_single_substitute_german_usascii_abbreviations(value,s_Oe,r_Oe);
	adjusted = adjusted | deen_keywords_single_substitute_german_usascii_abbreviations(value,s_Ae,r_Ae);
	adjusted = adjusted | deen_keywords_single_substitute_german_usascii_abbreviations(value,s_Ie,r_Ie);
	adjusted = adjusted | deen_keywords_single_substitute_german_usascii_abbreviations(value,s_ss,r_ss);

	return adjusted;
}


// happily the adjustments are applied to two consecutive characters which can
// be replaced in-situ with the UTF-8 sequence.

deen_bool deen_keywords_adjust(deen_keywords *keywords) {
	deen_bool adjusted = DEEN_FALSE;

	for (uint32_t i=0;i < keywords->count; i++) {
		adjusted = adjusted | deen_keywords_substitute_german_usascii_abbreviations(keywords->keywords[i]);
    }

    return adjusted;
}
