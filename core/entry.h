/*
 * Copyright 2016, Andrew Lindesay. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Andrew Lindesay, apl@lindesay.co.nz
 */

#ifndef __ENTRY_H
#define __ENTRY_H

#include "common.h"

deen_entry deen_entry_create(const uint8_t *german, const uint8_t *english);

void deen_entry_free(deen_entry *entry);

int deen_entry_calculate_distance_from_keywords(
	deen_entry *entry,
	deen_keywords *keywords,
	deen_bool *keyword_use_map);

#endif /* __ENTRY_H */
