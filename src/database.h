

#ifndef _DATABASE_H_
#define _DATABASE_H_
#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <limits.h>
#include "params.h"


sqlite3 * db_open (const char *path);
int db_close (sqlite3 * db);
int db_insertar(const char *path, const char *shasum, const char *datapath, unsigned int size, int deduplicados);
int db_get(const char *path, struct db_entry *entrada);
int db_get_datapath_hash(const char * hash, char * path, int * deduplicados);
void db_incrementar_duplicados(const char * hash);
void db_decrementar_duplicados(const char * hash);
int db_eliminar(const char * path);

int db_addLink(const char * enlace, const char * pathoriginal);
int db_getLinkPath(const char * enlace, char * pathoriginal);
int db_unlink(const char * path);


sqlite3 * map_open ();
int map_close();
int map_add(unsigned long long int fh, const char * path, int deduplicado, char modificado);
int map_extract(unsigned long long int fh, struct map_entry * entrada, int eliminar);
int map_count(const char * path);
void map_set_modificado(unsigned long long int fh);

#endif
