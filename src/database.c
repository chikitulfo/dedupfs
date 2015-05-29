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
    // Crear las tablas si no están creadas
    char * peticion =
    		"CREATE TABLE IF NOT EXISTS files("
    			"path TEXT PRIMARY KEY ON CONFLICT FAIL,"
    			"shasum TEXT NOT NULL ON CONFLICT FAIL,"
    			"datapath TEXT NOT NULL ON CONFLICT FAIL,"
    			"size INTEGER NOT NULL ON CONFLICT FAIL,"
    			"deduplicados INTEGER NOT NULL ON CONFLICT FAIL"
    		");"
    		"CREATE TABLE IF NOT EXISTS links("
    			"linkpath TEXT PRIMARY KEY ON CONFLICT FAIL,"
    			"originalpath TEXT NOT NULL ON CONFLICT FAIL"
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
 * Se introduce esta entrada en la base de datos de ficheros. Si ya existe se reemplaza
 */
int db_insertar(const char * path, const char * shasum, const char * datapath, unsigned int size, int deduplicados){
	int rc;
	static sqlite3_stmt * stmt;
	sqlite3 * mapa = BB_DATA->db;
	sqlite3_prepare_v2(mapa,"INSERT OR REPLACE INTO files VALUES ( ?1, ?2, ?3, ?4, ?5)", -1,&stmt,NULL);
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
		log_msg("       ERROR recuperando: %s\n", sqlite3_errmsg(mapa));
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
	log_msg("    db_incrementar_duplicados(%s)\n", hash);
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
 * Se decrementa la cuenta de duplicados en cada entrada que coincida con el hash.
 */
void db_decrementar_duplicados(const char * hash){
	static sqlite3_stmt *stmt;
	sqlite3 * mapa = BB_DATA->db;
	int rc;
	log_msg("    db_decrementar_duplicados(%s)\n", hash);
	sqlite3_prepare_v2(mapa,"UPDATE files SET deduplicados = deduplicados-1 "
			"WHERE (shasum = ?1)", -1,&stmt,NULL);
	sqlite3_bind_text(stmt,1,hash,-1,SQLITE_STATIC);
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
			log_msg("       ERROR decrementando duplicados: %s\n", sqlite3_errmsg(mapa));
	}
	sqlite3_finalize(stmt);
}

/**
 * Elimina una entrada de la base de datos.
 */
int db_eliminar(const char * path){
	static sqlite3_stmt *stmt;
	sqlite3 * mapa = BB_DATA->db;
	int rc;
	log_msg("    db_eliminar(%s)\n",path);
	sqlite3_prepare_v2(mapa,"DELETE FROM files WHERE (path = ?1)", -1,&stmt,NULL);
	sqlite3_bind_text(stmt,1,path,-1,SQLITE_STATIC);
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
			log_msg("       ERROR eliminando en db: %s\n", sqlite3_errmsg(mapa));
			rc = 0;
	}
	sqlite3_finalize(stmt);
	rc=1;
	return rc;
}

/**
 * Añade una entrada a la tabla de enlaces físicos, enlace es el path enlazado,
 * pathoriginal es el path del archivo creado originalmente.
 * Los enlaces físicos no hacen distinción entre enlace y enlazado, aquí es
 * necesario puesto que en la tabla de archivos deduplicados utilizamos el path
 * del archivo original.
 * También se desreferencia hasta apuntar al último nodo
 * Devuelve true  si se ha insertado, false si no.
 */
int db_addLink(const char * pathoriginal, const char * enlace){
	int rc, retval=1;
	static sqlite3_stmt * stmt;
	sqlite3 * db = BB_DATA->db;
	char * desreferenciado = strdup(pathoriginal);
	char * desreferenciar = strdup(pathoriginal);
	char * auxptr;
	//Desreferenciamos el pathoriginal en caso de que sea necesario.
	log_msg("    db_addlink(%s, %s)\n", pathoriginal, enlace);
	while(db_getLinkPath(desreferenciar,desreferenciado)) {
		auxptr = desreferenciado;
		desreferenciado = desreferenciar;
		desreferenciar = auxptr;
		log_msg("        derp\n");
	}

	sqlite3_prepare_v2(db,"INSERT INTO links VALUES ( ?1, ?2)", -1,&stmt,NULL);
	log_msg("      desreferenciado: %s\n", desreferenciar);
	sqlite3_bind_text(stmt,1,enlace,-1,SQLITE_STATIC);
	sqlite3_bind_text(stmt,2,desreferenciar,-1,SQLITE_STATIC);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_msg("      ERROR: %s\n", sqlite3_errmsg(db));
		retval = 0;
	}
	sqlite3_finalize(stmt);
	free(desreferenciado);
	free(desreferenciar);
	return retval;
}

/**
 * Recupera el path del archivo original al que apunta enlace, y lo guarda en
 * pathoriginal.
 * Devuelve true si se ha recuperado, false si no.
 */
int db_getLinkPath(const char * enlace, char * pathoriginal) {
	static sqlite3_stmt *stmt;
	sqlite3 * db = BB_DATA->db;
	int rc,found=0;

	sqlite3_prepare_v2(db,"SELECT originalpath FROM links WHERE (linkpath = ?1)", -1,&stmt,NULL);
	sqlite3_bind_text(stmt,1,enlace,-1,SQLITE_STATIC);
	log_msg("    db_getLinkPath(%s)\n", enlace);
	rc = sqlite3_step(stmt);
	log_msg("      rc_code= %d\n", rc);
	if (rc == SQLITE_ROW) {//Devuelve una fila. recuperamos los valores
		strcpy (pathoriginal, (char *)sqlite3_column_text(stmt,0));

		log_msg("      Encontrada: %s, %s\n", enlace, pathoriginal);
		found=1;
	}
	else if (rc != SQLITE_DONE) {
		log_msg("       ERROR recuperando: %s\n", sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return found;
}

/*
 * Elimina el enlace indicado por path.
 * Devuelve true si se ha eliminado, false en caso contrario.
 */
int db_unlink(const char * path) {
	int rc;
	static sqlite3_stmt * stmt;
	sqlite3 * db = BB_DATA->db;
    log_msg("    db_unlink(%s)\n",path);

	// Probamos a eliminar una entrada con linkpath = path
	sqlite3_prepare_v2(db,"DELETE fom links where linkpath = ?1", -1,&stmt,NULL);
	sqlite3_bind_text(stmt,1,path,-1,SQLITE_STATIC);
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		log_msg("      ERROR: %s\n", sqlite3_errmsg(db));
	    return 0;
	}
    if (sqlite3_changes(db)==0) {
    	return 0;
    }
	return 1;
}

/**
 * Busca en la tabla de enlaces uno que haga referencia a path y lo devuelve
 * en heredero. Se utiliza cuando es necesario eliminar la referencia de la
 * tabla de archivos, y hay que mirar si hay un heredero.
 * Devuelve true si ha encontrado heredero, y false en caso contrario
 */
int db_link_get_heredero(const char * path, char *heredero){
	static sqlite3_stmt *stmt;
	sqlite3 * db = BB_DATA->db;
	int rc,found=0;

	sqlite3_prepare_v2(db,"select linkpath from links where originalpath = ?1 limit 1", -1,&stmt,NULL);
	sqlite3_bind_text(stmt,1,path,-1,SQLITE_STATIC);
	log_msg("    db_linkget_heredero(%s)\n", path);
	rc = sqlite3_step(stmt);
	log_msg("      rc_code= %d\n", rc);
	if (rc == SQLITE_ROW) {//Devuelve una fila. recuperamos los valores
		strcpy (heredero, (char *)sqlite3_column_text(stmt,0));
		log_msg("      Encontrada: %s, %s\n", path, heredero);
		found=1;
	}
	else if (rc != SQLITE_DONE) {
		log_msg("       ERROR recuperando: %s\n", sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return found;
}

/**
 * Modifica todas las referencias que apunten a path en la tabla de enlaces,
 * y las sustituye por heredero.
 * Modifica la referencia con 'path' == path en la tabla de ficheros, en caso
 * de haberla.
 * Devuelve true si se han realizado cambios, false si no.
 */
int db_link_heredar(const char *path, const char *heredero){
	int rc, cambios=0;
	sqlite3 * db = BB_DATA->db;
    log_msg("    db_link_heredar(%s, %s); ",path, heredero);
    //Se modifican las referencias a path por heredero, y se elimina en caso de
    // que haya una tras la modificación que se referencie a sí misma.
    char *dbErrMsg=0, *peticion;
    asprintf(&peticion,"UPDATE files SET path = '%s' WHERE path = '%s' ;"
			"UPDATE links SET originalpath = '%s' WHERE originalpath = '%s' ;"
			"DELETE from links where linkpath = originalpath;",
    		heredero,path,heredero,path);
    rc = sqlite3_exec(db, peticion, /*callback*/NULL, 0, &dbErrMsg);
    free(peticion);
	cambios += sqlite3_changes(db);
    if( rc!=SQLITE_OK ){
    	log_msg("      Error accediendo a la base de datos: %s\n", dbErrMsg);
    	sqlite3_free(dbErrMsg);
    	abort();
    }
	log_msg("cambios: %d", cambios);
	if ( cambios==0){
		return 0;
	}
	return 1;
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
    if( rc ){
        log_msg("      Can't open database: %s\n", sqlite3_errmsg(mapa));
        sqlite3_close(mapa);
        abort();
    }
    log_msg("      bd mem_mapa() creada\n");
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
int map_add(unsigned long long int fh,const char * path, int deduplicado, char modificado){
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
		entrada->deduplicados = sqlite3_column_int(stmt_select,2);
		entrada->modificado = sqlite3_column_int(stmt_select,3);
		log_msg("      Encontrada fila: %llu, %s, %d, %d\n",
				entrada->fh, entrada->path, entrada->deduplicados, entrada->modificado);
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
