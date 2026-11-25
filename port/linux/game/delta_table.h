#ifndef DELTA_TABLE_H
#define DELTA_TABLE_H
#include "delta_layout.h"
/* Delta's signed legacy table (port/linux/src/delta_table.c): its serial
(0: none), and its layout facts past the built-in versions */
unsigned long delta_table_serial(void);
/* a version's layout, built in and with the table's facts (delta_layout_of):
1 if it has one */
int delta_table_layout_of(unsigned version, struct delta_layout *layout);
/* a signed table checked as a fetched one would be, and taken if newer: 1 if taken */
int delta_table_check(const char *signed_table, unsigned long size);
#endif
