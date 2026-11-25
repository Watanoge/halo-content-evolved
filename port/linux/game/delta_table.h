#ifndef DELTA_TABLE_H
#define DELTA_TABLE_H
#include "delta_layout.h"
/* Delta's signed legacy table (port/linux/src/delta_table.c): the newest
network version a row joins (0: no table), its serial (0: none), and its
layout facts past the built-in versions applied to a version's layout */
unsigned delta_table_newest_version(void);
unsigned long delta_table_serial(void);
void delta_table_apply_layouts(struct delta_layout *layout);
/* a signed table checked as a fetched one would be, and taken if newer: 1 if taken */
int delta_table_check(const char *signed_table, unsigned long size);
#endif
