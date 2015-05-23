

#ifndef _DATABASE_H_
#define _DATABASE_H_
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <limits.h>
#include "params.h"


sqlite3 * opendb (const char *path);
int closedb (sqlite3 * db);
sqlite3 * map_open ();
int map_close();
int map_add(unsigned long long int fh, const char * path, char deduplicado, char modificado);
int map_extract(unsigned long long int fh, struct map_entry * entrada, int eliminar);
int map_count(const char * path);

#endif
