/*
  Módulo de dedupfs encargado de la interacción con
  la base de datos sqlite utilizada.
*/
#define _GNU_SOURCE

#include "log.h"
#include "database.h"

#include <string.h>



sqlite3 * db_open (const char *path) {
    sqlite3 *db;
    int rc;
    char *dbErrMsg = 0;

    rc = sqlite3_open(path, &db);
    if( rc ){
        log_msg("      Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        abort();
    }
    // Crear la tabla si no está creada
    char * peticion =
    		"CREATE TABLE IF NOT EXISTS files("
    			"path TEXT PRIMARY KEY ON CONFLICT FAIL,"
    			"shasum TEXT NOT NULL ON CONFLICT FAIL,"
    			"datapath TEXT NOT NULL ON CONFLICT FAIL,"
    			"size INTEGER NOT NULL ON CONFLICT FAIL,"
    			"deduplicados INTEGER NOT NULL ON CONFLICT FAIL"
    			");";
    rc = sqlite3_exec(db, peticion, /*callback*/NULL, 0, &dbErrMsg);
    if( rc!=SQLITE_OK ){
    	log_msg("      Error accediendo a la base de datos\n"
    			"      SQL error: %s\n", dbErrMsg);
    	sqlite3_free(dbErrMsg);
    	abort();
    }
    return db;
}

int db_close (sqlite3 *db) {
    return sqlite3_close(db);
}
/**
 * Se introduce esta entrada en la base de datos de ficheros
 */
int db_insertar(const char * path, const char * shasum, const char * datapath, unsigned int size, int deduplicados){
	int rc;
	static sqlite3_stmt * stmt;
	sqlite3 * mapa = BB_DATA->db;
	sqlite3_prepare_v2(mapa,"INSERT INTO files VALUES ( ?1, ?2, ?3, ?4, ?5)", -1,&stmt,NULL);
	log_msg("    db_insertar(%s, %s, %s, %u, %d)\n", path, shasum, datapath, size, deduplicados);
	sqlite3_bind_text(stmt,1,path,-1,SQLITE_STATIC);
	sqlite3_bind_text(stmt,2,shasum,-1,SQLITE_STATIC);
	sqlite3_bind_text(stmt,3,datapath,-1,SQLITE_STATIC);
	sqlite3_bind_int(stmt, 4, size);
	sqlite3_bind_int(stmt, 5, deduplicados);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_msg("      ERROR inserting data: %s\n", sqlite3_errmsg(mapa));
	    return 0;
	}
	sqlite3_finalize(stmt);
	return 1;
}

/**
 * Recupera la entrada correspondiente a path.
 * Devuelve 1 si se ha recuperado, 0 en caso contrario.
 */
int db_get(const char * path, struct db_entry *entrada){
	static sqlite3_stmt *stmt;
	sqlite3 * mapa = BB_DATA->db;
	int rc,found=0;

	sqlite3_prepare_v2(mapa,"SELECT * FROM files WHERE (path = ?1)", -1,&stmt,NULL);
	sqlite3_bind_text(stmt,1,path,-1,SQLITE_STATIC);
	log_msg("    db_get(%s)\n", path);
	rc = sqlite3_step(stmt);
	log_msg("      rc_code= %d\n", rc);
	if (rc == SQLITE_ROW) {//Devuelve una fila. recuperamos los valores
		strcpy (entrada->path, (char *)sqlite3_column_text(stmt,0));
		strcpy (entrada->sha1sum, (char *)sqlite3_column_text(stmt,1));
		strcpy (entrada->datapath, (char *)sqlite3_column_text(stmt,2));
		entrada->size = sqlite3_column_int(stmt,3);
		entrada->deduplicados = sqlite3_column_int(stmt,4);
		log_msg("      Encontrada: %s, %s, %s, %u, %d\n", entrada->path, entrada->sha1sum,
				entrada->datapath, entrada->size, entrada->deduplicados);
		found=1;
	}
	else if (rc != SQLITE_DONE) {
		log_msg("       ERROR recuperando del mapa: %s\n", sqlite3_errmsg(mapa));
	}
	sqlite3_finalize(stmt);
	return found;
}

/**
 * Recupera el datapath de un hash dado, y la cuenta de deduplicados.
 * Devuelve 1 si se ha encontrado, y 0 en caso contrario.
 */
int db_get_datapath_hash(const char * hash, char * path, int * deduplicados) {
	static sqlite3_stmt *stmt;
	sqlite3 * mapa = BB_DATA->db;
	int rc,found=0;

	sqlite3_prepare_v2(mapa,"SELECT datapath,deduplicados FROM files WHERE (shasum = ?1)",
						-1,&stmt,NULL);
	sqlite3_bind_text(stmt,1,hash,-1,SQLITE_STATIC);
	log_msg("    db_get_datapath_hash(%s)\n", hash);
	rc = sqlite3_step(stmt);
	log_msg("      rc_code= %d\n", rc);
	if (rc == SQLITE_ROW) {//Devuelve una fila. recuperamos los valores
		strcpy (path, (char *)sqlite3_column_text(stmt,0));
		*deduplicados = sqlite3_column_int(stmt,1);
		log_msg("      Encontrada: %s, %d\n", path,*deduplicados);
		found=1;
	}
	else if (rc != SQLITE_DONE) {
		log_msg("       ERROR recuperando del mapa: %s\n", sqlite3_errmsg(mapa));
	}
	sqlite3_finalize(stmt);
	return found;
}

/**
 * Se incrementa la cuenta de duplicados en cada entrada que coincida con el hash.
 */
void db_incrementar_duplicados(const char * hash){
	static sqlite3_stmt *stmt;
	sqlite3 * mapa = BB_DATA->db;
	int rc;
	log_msg("    db_incrementar_duplicados(%s)", hash);
	sqlite3_prepare_v2(mapa,"UPDATE files SET deduplicados = deduplicados+1 "
			"WHERE (shasum = ?1)", -1,&stmt,NULL);
	sqlite3_bind_text(stmt,1,hash,-1,SQLITE_STATIC);
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
			log_msg("       ERROR incrementando duplicados: %s\n", sqlite3_errmsg(mapa));
	}
	sqlite3_finalize(stmt);
}


/**
 * El mapa de ficheros abiertos se guarda en una base de datos en memoria
 * Para cada fichero, guarda cuantas veces está abierto para escritura,
 * y si ha sido modificado.
 */
sqlite3 * map_open() {
    sqlite3 * mapa;
    int rc;
    char * dbErrMsg = 0;
    rc = sqlite3_open(":memory:", &mapa);
    char * path; //DEBUG
    asprintf(&path, "%s/.dedupfs/memdata.db", BB_DATA->rootdir); //DEBUG
    //rc = sqlite3_open(path, &mapa); //DEBUG
    free(path);
    if( rc ){
        log_msg("      Can't open database: %s\n", sqlite3_errmsg(mapa));
        sqlite3_close(mapa);
        abort();
    }
    log_msg("      bd mem_mapa() creada\n");
    //BEGIN DEBUG TODO ELIMINAR Cuando la pasemos a memoria
    rc=sqlite3_exec(mapa, "DROP TABLE IF EXISTS mapa", /*callback*/NULL, 0, &dbErrMsg); //DEBUG
    if( rc!=SQLITE_OK ){
    	log_msg("      Error accediendo a la base de datos: %s\n", dbErrMsg);
    	sqlite3_free(dbErrMsg);
    	abort();
    }
    //END DEBUG
    // Crear la tabla si no está creada
    char * peticion =
    		"CREATE TABLE IF NOT EXISTS mapa("
    		  "fh INTEGER PRIMARY KEY ON CONFLICT FAIL,"
    		  "path TEXT NOT NULL ON CONFLICT FAIL,"
    		  "deduplicado INTEGER DEFAULT 0,"
    		  "modificado INTEGER DEFAULT 0"
    			");";
    rc = sqlite3_exec(mapa, peticion, /*callback*/NULL, 0, &dbErrMsg);
    if( rc!=SQLITE_OK ){
    	log_msg("      Error accediendo a la base de datos: %s\n", dbErrMsg);
    	sqlite3_free(dbErrMsg);
    	abort();
    }
    return mapa;
}

int map_close () {
    return sqlite3_close(BB_DATA->mapopenw);
}

/**
 * Se inserta un nuevo descriptor de fichero en el mapa de ficheros abiertos.
 */
int map_add(unsigned long long int fh,const char * path, char deduplicado, char modificado){
	int rc;
	static sqlite3_stmt * stmt;
	sqlite3 * mapa = BB_DATA->mapopenw;
	sqlite3_prepare_v2(mapa,"INSERT INTO mapa VALUES ( ?1, ?2, ?3, ?4)", -1,&stmt,NULL);
	log_msg("    map_add(%llu, %s, %d, %d)\n", fh, path, deduplicado, modificado);
	sqlite3_bind_int64(stmt,1,fh);
	sqlite3_bind_text(stmt,2,path,-1,SQLITE_STATIC);
	sqlite3_bind_int(stmt, 3, deduplicado);
	sqlite3_bind_int(stmt, 4, modificado);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_msg("      ERROR inserting data: %s\n", sqlite3_errmsg(mapa));
	    return 0;
	}
	sqlite3_finalize(stmt);
	return 1;
}
/**
 * Se recupera la entrada del descriptor fh si existe, y se guarda en entrada.
 * Si eliminar es true, además se elimina de la tabla en caso de existir.
 * Devuelve 1 si se ha recuperado, 0 si no
 */
int map_extract(unsigned long long int fh, struct map_entry * entrada, int eliminar){
	static sqlite3_stmt *stmt_select, *stmt_delete;
	sqlite3 * mapa = BB_DATA->mapopenw;
	int rc,found=0;
	
	sqlite3_prepare_v2(mapa,"SELECT * FROM mapa WHERE (fh = ?1)", -1,&stmt_select,NULL);
	sqlite3_bind_int64(stmt_select,1,fh);
	log_msg("    map_extract(%llu, %d)\n", fh, eliminar);
	rc = sqlite3_step(stmt_select);
	log_msg("      rc_code= %d\n", rc);
	if (rc == SQLITE_ROW) {//Devuelve una fila. recuperamos los valores
		entrada->fh=sqlite3_column_int64(stmt_select,0);
		strcpy (entrada->path, (char *)sqlite3_column_text(stmt_select,1));
		entrada->deduplicado = sqlite3_column_int(stmt_select,2);
		entrada->modificado = sqlite3_column_int(stmt_select,3);
		log_msg("      Encontrada fila: %llu, %s, %d, %d\n",
				entrada->fh, entrada->path, entrada->deduplicado, entrada->modificado);
		if (eliminar){ //Eliminamos si se solicita
			sqlite3_prepare_v2(mapa,"DELETE FROM mapa WHERE (fh = ?1)", -1,&stmt_delete,NULL);
			sqlite3_bind_int64(stmt_delete,1,fh);
			rc = sqlite3_step(stmt_delete);
			if (rc != SQLITE_DONE) {
					log_msg("       ERROR eliminando en mapa: %s\n", sqlite3_errmsg(mapa));
			}
			sqlite3_finalize(stmt_delete);
		}
		found=1;
	}
	else if (rc != SQLITE_DONE) {
		log_msg("       ERROR recuperando del mapa: %s\n", sqlite3_errmsg(mapa));
	}
	sqlite3_finalize(stmt_select);
	return found;
}

/**
 * Devuelve el número de entradas en el mapa para una misma ruta de archivo
 */
int map_count(const char * path){
	int rc,count;
	static sqlite3_stmt *stmt_count;
	sqlite3 * mapa = BB_DATA->mapopenw;
	sqlite3_prepare_v2(mapa,"SELECT COUNT(fh) FROM mapa WHERE (path = ?1)", -1,&stmt_count,NULL);
	sqlite3_bind_text(stmt_count,1,path,-1,SQLITE_STATIC);
	rc = sqlite3_step(stmt_count);
	if (rc == SQLITE_ROW) {//Devuelve una fila. recuperamos el valor
		count = sqlite3_column_int(stmt_count,0);
		log_msg("    map_count(%s) = %d\n", path, count);
	}
	else if (rc != SQLITE_DONE) {
		log_msg("       ERROR map_count(): %s\n", sqlite3_errmsg(mapa));
	    count=-1;
	}
	sqlite3_finalize(stmt_count);
	return count;
}

/**
 *  Actualiza las entradas del mapa que coincidan con path,
 *  activando el flag modificado
 */
void map_set_modificado(unsigned long long int fh){
	int rc;
	char *dbErrMsg, *peticion;
	sqlite3 * mapa = BB_DATA->mapopenw;
    asprintf(&peticion, "UPDATE mapa SET modificado = 1 WHERE fh=%llu", fh);
    rc = sqlite3_exec(mapa, peticion, /*callback*/NULL, 0, &dbErrMsg);
    log_msg("    map_set_modificado(%llu)\n",fh);
    free(peticion);
    if( rc!=SQLITE_OK ){
    	log_msg("      Error accediendo a la base de datos: %s\n", dbErrMsg);
    	sqlite3_free(dbErrMsg);
    }
}
