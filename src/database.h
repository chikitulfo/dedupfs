

#ifndef _DATABASE_H_
#define _DATABASE_H_
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

sqlite3 * opendb (const char *path);
int closedb (sqlite3 * db);

#endif