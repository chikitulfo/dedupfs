/*
  Módulo de dedupfs encargado de la interacción con
  la base de datos sqlite utilizada.
*/

#include "database.h"

sqlite3 * opendb (const char *path) {
    sqlite3 * db;
    int rc;
    rc = sqlite3_open(path, &db);
    if( rc ){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        abort();
    }
    return db;
}

int closedb (sqlite3 * db) {
    return sqlite3_close(db);
}