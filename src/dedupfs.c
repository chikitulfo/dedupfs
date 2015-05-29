
#include "params.h"

#include <fuse.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <openssl/evp.h>


#include "log.h"
#include "database.h"


// Report errors to logfile and give -errno to caller
static int bb_error(char *str)
{
    int ret = -errno;

    log_msg("    ERROR %s: %s\n", str, strerror(errno));

    return ret;
}

/**
 * Calcula el sha1sum de path y lo devuelve como cadena en outHashString
 */
static void calcular_hash(const char * path, char * outHashString, unsigned int * filesize){
	ssize_t leidos;
	unsigned int sizedigest, i;
	int fd;
	unsigned char hash[20];
	char buffer[4096], *outptr;

	if ( (fd = open(path,O_RDONLY)) == -1){
		bb_error("[calcular_hash]open");
	}
	*filesize = 0;
	EVP_MD_CTX *context = EVP_MD_CTX_create();
	EVP_DigestInit_ex(context, EVP_sha1(), NULL);
	//Leer desde el principio
	if(lseek(fd, 0, SEEK_SET)<0){
		bb_error("[calcular_hash]lseek");
	}
	leidos = read(fd,buffer,4096);
	if(leidos<0){
		bb_error("[calcular_hash]read");
	}
	else{
		*filesize += leidos;
		EVP_DigestUpdate(context,buffer,leidos);
		while (leidos == 4096){
			leidos = read(fd,buffer,4096);
			*filesize += leidos;
			EVP_DigestUpdate(context,buffer,leidos);
		}
	}
	close(fd);
	EVP_DigestFinal_ex(context,hash,&sizedigest);
	EVP_MD_CTX_destroy(context);

	//Introducirlo en la cadena outHashString
	outptr = outHashString;
	for(i=0; i<sizedigest; i++){
		//log_msg("%02x", hash[i]);
		sprintf(outptr,"%02x", hash[i]);
		outptr+=2;
	}
	outptr[0]=0; //Terminar la cadena en 0
}

/**
 * Función encargada de asegurar que los directorios necesarios para guardar
 * los datos en el directorio de datos están creados.
 * El path donde se almacenen será .dedupfs/data/X/Y/Z/HASH donde X, Y y Z son
 * los tres primeros caracteres del hash.
 * Devuelve en datapath la ruta donde se almacenan los datos de ese hash.
 * (Relativa a la raiz del sf fuse)
 */
static void preparar_datapath(char * hash, char * datapath) {
	int rv;
	log_msg("    preparar_datapath(%s)\n",hash);

	sprintf(datapath, "%s/.dedupfs/data/%c/", BB_DATA->rootdir, hash[0]);
	rv = mkdir(datapath, 0777); 	//Intentamos crear X, y asumimos el error EEXIST
	if(rv != 0 && errno != EEXIST)
		bb_error("mkdir: ");
	sprintf(datapath, "%s%c/", datapath, hash[1]);
	rv = mkdir(datapath, 0777); 	//Intentamos crear Y, y asumimos el error EEXIST
	if(rv != 0 && errno != EEXIST)
		bb_error("mkdir: ");
	sprintf(datapath, "%s%c/", datapath, hash[2]);
	rv = mkdir(datapath, 0777); 	//Intentamos crear Z, y asumimos el error EEXIST
	if(rv != 0 && errno != EEXIST)
		bb_error("mkdir: ");
	//Devolver sin rootdir
	sprintf(datapath, "/.dedupfs/data/%c/%c/%c/%s", hash[0], hash[1], hash[2], hash);
}

/**
 * Copia un archivo en otro.
 * Utilizado en las copias de escritura de archivos deduplicados
 */
static void copiar ( const char * src, const char * dst){
	unsigned char buffer[4096];
	int fdsrc, fddst, leidos, escritos;
	log_msg("    copiar( %s ;\n"
			"            %s\n", src, dst);
	if ( (fdsrc = open(src,O_RDONLY)) == -1){
		bb_error("[copiar]open src");
	}
	else if ((fddst = creat(dst,0777)) == -1){
		bb_error("[copiar]create dst");
	}
	else {
		leidos = read(fdsrc,buffer,4096);
		escritos = write(fddst,buffer,leidos);
		if(leidos<0 || escritos<0){
			bb_error("[copiar]read/write");}
		else{
			while (leidos == 4096){
				leidos = read(fdsrc,buffer,4096);
				escritos = write(fddst,buffer,leidos);
				if(leidos<0 || escritos<0){
					bb_error("[copiar]read/write");}
			}
		}
	}
	close(fdsrc);
	close(fddst);
}

//  All the paths I see are relative to the root of the mounted
//  filesystem.  In order to get to the underlying filesystem, I need to
//  have the mountpoint.  I'll save it away early on in main(), and then
//  whenever I need a path for something I'll call this to construct
//  it.
static void bb_fullpath(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, BB_DATA->rootdir);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will
            // break here

    log_msg("    bb_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n",
      BB_DATA->rootdir, path, fpath);
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given.
 */
int bb_getattr(const char *path, struct stat *statbuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    struct db_entry entradabd;
    
    log_msg("\nbb_getattr(path=\"%s\", statbuf=0x%08x)\n",
    path, statbuf);
    bb_fullpath(fpath, path);
    
    retstat = lstat(fpath, statbuf);
    if (retstat != 0)
    	retstat = bb_error("bb_getattr lstat");
    
    // Desreferenciamos path si es un enlace duro
    if(db_getLinkPath(path, fpath)) {
    	//Si es true se ha desreferenciado y guardado en fpath.
    	//Cambiamos el puntero de path a fpath
    	path=fpath;
    }
    //Si el archivo está en la bd, el tamaño se toma de ahí
    if( db_get(path,&entradabd)){
    	statbuf->st_size = entradabd.size;
    	//Necesario incluir st_blocks? Lo incluimos
    	//damos por supuesto tamaño de bloque de 4096
    	statbuf->st_blocks = (entradabd.size / 4096 + (entradabd.size % 4096 > 0))*8;
    }
    log_stat(statbuf);

    return retstat;
}

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */
// Note the system readlink() will truncate and lose the terminating
// null.  So, the size passed to to the system readlink() must be one
// less than the size passed to bb_readlink()
// bb_readlink() code by Bernardo F Costa (thanks!)
int bb_readlink(const char *path, char *link, size_t size)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("bb_readlink(path=\"%s\", link=\"%s\", size=%d)\n",
	  path, link, size);
    bb_fullpath(fpath, path);

    retstat = readlink(fpath, link, size - 1);
    log_msg("    link: %s", link);
    if (retstat < 0)
	retstat = bb_error("bb_readlink readlink");
    else  {
	link[retstat] = '\0';
	retstat = 0;
    }

    return retstat;
}

/** Create a file node
 *
 * There is no create() operation, mknod() will be called for
 * creation of all non-directory, non-symlink nodes.
 */
// shouldn't that comment be "if" there is no.... ?
int bb_mknod(const char *path, mode_t mode, dev_t dev)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_mknod(path=\"%s\", mode=0%3o, dev=%lld)\n",
	  path, mode, dev);
    bb_fullpath(fpath, path);

    // On Linux this could just be 'mknod(path, mode, rdev)' but this
    //  is more portable
    if (S_ISREG(mode)) {
        retstat = open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode);
	if (retstat < 0)
	    retstat = bb_error("bb_mknod open");
        else {
            retstat = close(retstat);
	    if (retstat < 0)
		retstat = bb_error("bb_mknod close");
	}
    } else
	if (S_ISFIFO(mode)) {
	    retstat = mkfifo(fpath, mode);
	    if (retstat < 0)
		retstat = bb_error("bb_mknod mkfifo");
	} else {
	    retstat = mknod(fpath, mode, dev);
	    if (retstat < 0)
		retstat = bb_error("bb_mknod mknod");
	}

    return retstat;
}

/** Create a directory */
int bb_mkdir(const char *path, mode_t mode)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_mkdir(path=\"%s\", mode=0%3o)\n",
	    path, mode);
    bb_fullpath(fpath, path);

    retstat = mkdir(fpath, mode);
    if (retstat < 0)
	retstat = bb_error("bb_mkdir mkdir");

    return retstat;
}

/** Remove a directory */
int bb_rmdir(const char *path)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("bb_rmdir(path=\"%s\")\n",
	    path);
    bb_fullpath(fpath, path);

    retstat = rmdir(fpath);
    if (retstat < 0)
	retstat = bb_error("bb_rmdir rmdir");

    return retstat;
}

/** Create a symbolic link */
// The parameters here are a little bit confusing, but do correspond
// to the symlink() system call.  The 'path' is where the link points,
// while the 'link' is the link itself.  So we need to leave the path
// unaltered, but insert the link into the mounted directory.
int bb_symlink(const char *path, const char *link)
{
    int retstat = 0;
    char flink[PATH_MAX];

    log_msg("\nbb_symlink(path=\"%s\", link=\"%s\")\n",
	    path, link);
    bb_fullpath(flink, link);

    retstat = symlink(path, flink);
    if (retstat < 0)
	retstat = bb_error("bb_symlink symlink");

    return retstat;
}

/** Create a hard link to a file */
int bb_link(const char *path, const char *newpath)
{
    int retstat = 0;
    char fpath[PATH_MAX], fnewpath[PATH_MAX];

    log_msg("\nbb_link(path=\"%s\", newpath=\"%s\")\n",
	    path, newpath);
    bb_fullpath(fpath, path);
    bb_fullpath(fnewpath, newpath);

    retstat = link(fpath, fnewpath);
    if (retstat < 0)
    	retstat = bb_error("bb_link link");
    else // Se inserta en la tabla de enlaces
    	db_addLink(path, newpath);

    return retstat;
}

/** Remove a file */
int bb_unlink(const char *path)
{
    int retstat = 0;
    char fpath[PATH_MAX];


    log_msg("bb_unlink(path=\"%s\")\n",
	    path);

    bb_fullpath(fpath, path);

    retstat = unlink(fpath);
    if (retstat < 0){
    	retstat = bb_error("bb_unlink unlink");
    }
    else {
        struct db_entry entradadb;
        if (db_get(path, &entradadb)){
        	char * heredero = malloc(PATH_MAX);
        	// Si está en la tabla de enlaces, su entrada de la tabla de enlaces
        	//  debe heredarla otro, y no se modifican los datos
        	if (db_link_get_heredero(path, heredero)){
        		db_link_heredar(path, heredero);
        	} else {
    			db_eliminar(path);
    			// Si no está deduplicado se eliminan los datos
        		if (entradadb.deduplicados == 0) {
        			bb_fullpath(fpath,entradadb.datapath);
        			unlink(fpath);
        		} else { // Si está deduplicado se decrementa el contador
        			db_decrementar_duplicados(entradadb.sha1sum);
        		}
        	}
        	free(heredero);
        } else {
        	//Si no está en la tabla de datos, se elimina de la de enlaces
        	// puesto que puede estar como enlace
            db_unlink(path);
        }
    }

    return retstat;
}

/** Change the permission bits of a file */
int bb_chmod(const char *path, mode_t mode)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_chmod(fpath=\"%s\", mode=0%03o)\n",
	    path, mode);
    bb_fullpath(fpath, path);

    retstat = chmod(fpath, mode);
    if (retstat < 0)
	retstat = bb_error("bb_chmod chmod");

    return retstat;
}

/** Change the owner and group of a file */
int bb_chown(const char *path, uid_t uid, gid_t gid)

{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_chown(path=\"%s\", uid=%d, gid=%d)\n",
	    path, uid, gid);
    bb_fullpath(fpath, path);

    retstat = chown(fpath, uid, gid);
    if (retstat < 0)
	retstat = bb_error("bb_chown chown");

    return retstat;
}

/** Change the access and/or modification times of a file */
/* note -- I'll want to change this as soon as 2.6 is in debian testing */
int bb_utime(const char *path, struct utimbuf *ubuf)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg("\nbb_utime(path=\"%s\", ubuf=0x%08x)\n",
      path, ubuf);
    bb_fullpath(fpath, path);
    
    retstat = utime(fpath, ubuf);
    if (retstat < 0)
      retstat = bb_error("bb_utime utime");
    
    return retstat;
}

/** File open operation
 *
 * No creation, or truncation flags (O_CREAT, O_EXCL, O_TRUNC)
 * will be passed to open().  Open should check if the operation
 * is permitted for the given flags.  Optionally open may also
 * return an arbitrary filehandle in the fuse_file_info structure,
 * which will be passed to all file operations.
 *
 * Changed in version 2.2
 */
int bb_open(const char *path, struct fuse_file_info *fi)
{
    int fd;
    char fpath[PATH_MAX], dereferecedp[PATH_MAX];
    struct db_entry entradadb;
    int retstat = 0;
    char escritura = 0;

    log_msg("\nbb_open(path\"%s\", fi=0x%08x)\n",
	    path, fi);

    if ((fi->flags&O_RDWR) == O_RDWR || (fi->flags&O_WRONLY) == O_WRONLY){
    	escritura = 1;
    }

    //Obtener auténtico path de datos si es un enlace.
	if ( db_getLinkPath(path, dereferecedp)){
		//Se ha obtenido el path original.
		path = dereferecedp;
	}

    if (db_get(path, &entradadb)) {
    	bb_fullpath(fpath, entradadb.datapath);
    	//Si está deduplicado y va a escribirse, hay que usar una copia.
    	if(escritura && entradadb.deduplicados > 0) {
    		//Se hace la copia si es el primero en abrirse
    		if (map_count(path) == 0){
				char * oldfpath = strdup(fpath);
	    		strcat(fpath, "w"); //Las copias de trabajo tienen una "w" al final
				copiar(oldfpath, fpath);
				free(oldfpath);
    		} else {
        		strcat(fpath, "w");
    		}
    	}
    }
    else {
    	bb_fullpath(fpath, path);
    	entradadb.deduplicados = 0; //para incluirlo en el mapa
    }
    log_msg("    open(%s)\n", fpath);
    fd = open(fpath, fi->flags);
    if (fd < 0)
	retstat = bb_error("bb_open open");

    fi->fh = fd;
    log_fi(fi);
    // Si se abre para escritura, añadir al mapa
    if (escritura){
    	map_add(fi->fh, path, entradadb.deduplicados, 0);
    }

    return retstat;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.  An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 *
 * Changed in version 2.2
 */
// I don't fully understand the documentation above -- it doesn't
// match the documentation for the read() system call which says it
// can return with anything up to the amount of data requested. nor
// with the fusexmp code which returns the amount of data also
// returned by read.
int bb_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);
    // no need to get fpath on this one, since I work from fi->fh not the path
    log_fi(fi);

    retstat = pread(fi->fh, buf, size, offset);
    if (retstat < 0)
	retstat = bb_error("bb_read read");

    return retstat;
}

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
// As  with read(), the documentation above is inconsistent with the
// documentation for the write() system call.
int bb_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi
	    );
    // no need to get fpath on this one, since I work from fi->fh not the path
    log_fi(fi);

    retstat = pwrite(fi->fh, buf, size, offset);
    if (retstat < 0)
	retstat = bb_error("bb_write pwrite");

    // Marcamos el archivo como modificado
    if(size >0){
    	map_set_modificado(fi->fh);
    }

    return retstat;
}

/** Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
int bb_statfs(const char *path, struct statvfs *statv)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_statfs(path=\"%s\", statv=0x%08x)\n",
	    path, statv);
    bb_fullpath(fpath, path);

    // get stats for underlying filesystem
    retstat = statvfs(fpath, statv);
    if (retstat < 0)
	retstat = bb_error("bb_statfs statvfs");

    log_statvfs(statv);

    return retstat;
}

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
int bb_flush(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    log_msg("\nbb_flush(path=\"%s\", fi=0x%08x)\n", path, fi);
    // no need to get fpath on this one, since I work from fi->fh not the path
    log_fi(fi);
  
    return retstat;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file descriptor.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 *
 * Changed in version 2.2
 */
int bb_release(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    struct map_entry entradamap;
    char nuevohash[41];
    log_msg("\nbb_release(path=\"%s\", fi=0x%08x)\n",
    path, fi);
    log_fi(fi);
    // Cerramos el archivo, y después vemos.
    retstat = close(fi->fh);
    // Obtenemos la información del mapa de ficheros abiertos en escritura
    if (map_extract(fi->fh, &entradamap, 1)  //Si está en el mapa
    		&& entradamap.modificado		 //y ha sido modificado
			&& map_count(entradamap.path)<1){//y era el último abierto
    	unsigned int size;
    	struct db_entry entradadb;
    	char truepath[PATH_MAX];
		char truedatapath[PATH_MAX];
		char dereferencedpath[PATH_MAX];

    	log_msg("    Abierto en escritura, modificado y último.\n");

    	//Si es un archivo enlazado, path contiene la direccion del enlace,
    	// mientras que la tabla de deduplicados está guardada con el path
    	// del que se enlaza. Hay que cambiar eso.

    	if ( db_getLinkPath(path, dereferencedpath)){
    		//Se ha obtenido el path original.
    		path = dereferencedpath;
    	}

    	//Si se encuentra en la base de datos, el archivo no es nuevo
    	if( db_get(path,&entradadb)) {
    		//El archivo no es nuevo, se ha modificado
    		if(entradamap.deduplicados>0) {
        		// Si estaba deduplicado, es una copia para escritura.
    			bb_fullpath(truedatapath, strcat(entradadb.datapath,"w"));
    		} else {
    			bb_fullpath(truedatapath, entradadb.datapath);
    		}
    		//Calculamos el hash
    		calcular_hash(truedatapath, nuevohash, &size);
    		log_msg("    NewHash: %s\n", nuevohash);
    		log_msg("    OldHash: %s\n", entradadb.sha1sum);
    		// Había una entrada en la bd. Comparar hashes y tamaño
        	if (size == 0){
        		// Si el tamaño es 0, se saca de la bd y punto.
        		db_eliminar(path);
        		if(entradadb.deduplicados==0){
        			// Borrar los datos también, es el último.
        			unlink(truedatapath);
        		} else {
        			// No es el último, decrementar
        			db_decrementar_duplicados(entradadb.sha1sum);
        		}
        	}
        	else if (strcmp(entradadb.sha1sum,nuevohash)) {
        		// Si el tamaño no es 0, y los hashes son diferentes
        		if (db_get_datapath_hash(nuevohash, entradadb.datapath,&(entradadb.deduplicados))){
            		// El nuevo hash ya está presente, eliminar
            		unlink(truedatapath);
    				db_insertar(path, nuevohash, entradadb.datapath, size, entradadb.deduplicados);
    				db_incrementar_duplicados(nuevohash);
        		}else {
        			//Es el primero, mover(si está deduplicado estamos moviendo la copia)
    				preparar_datapath(nuevohash, entradadb.datapath);
    				char * newdatapath = truepath; //newdatapath apunta a truepath pero se llama diferente por claridad
    				bb_fullpath(newdatapath, entradadb.datapath);
        			if (rename(truedatapath,newdatapath)) {
        				log_msg("    rename(%s,%s)",truedatapath,newdatapath);
    					bb_error("rename");}
    				db_insertar(path, nuevohash, entradadb.datapath, size, 0);
        		}
				db_decrementar_duplicados(entradadb.sha1sum);
        	}
    	} else {
        	bb_fullpath(truepath, path);
        	//Calculamos el hash
        	calcular_hash(truepath, nuevohash, &size);
    		log_msg("    NewHash: %s\n", nuevohash);
    		//No había entrada con este path, se inserta y nada más
    		if (size > 0){ //Si el tamaño es mayor que 0, hay que insertar
    			if (db_get_datapath_hash(nuevohash, entradadb.datapath, &(entradadb.deduplicados))){
    				//Si el hash ya está en la bd, añadimos este archivo
    				db_insertar(path, nuevohash, entradadb.datapath, size, entradadb.deduplicados);
    				db_incrementar_duplicados(nuevohash);
    				//Truncamos el archivo en el lugar original
    				truncate(truepath,0);
    			} else { //Único archivo con este hash
    				//Mover a la dirección del hash. Hay que dejar un archivo vacío.
    				//conservamos los permisos originales
    				int fd ;
    				struct stat *prestat = malloc(sizeof(struct stat));
    				if (stat(truepath,prestat)) {
    					bb_error("stat al mover");}
    				//obtenemos el path donde se van a almacenar los datos
    				preparar_datapath(nuevohash, entradadb.datapath);
    				bb_fullpath(truedatapath,entradadb.datapath);
    				//movemos los datos a la carpeta de datos
    				if (rename(truepath, truedatapath)) {
    					bb_error("rename");}
    				//Volvemos a crear el archivo del path original, con sus permisos
    				fd = creat(truepath,prestat->st_mode);
    				if (fd == -1) {
    					bb_error("Create tras mover a datadir");}
    				close(fd);
    				free(prestat);
    				//insertamos la entrada en la base de datos
    				db_insertar(path, nuevohash, entradadb.datapath, size, 0);
    			}
    		}
    	}
    }
    return retstat;
}

/** Synchronize file contents
 *
 * If the datasync parameter is non-zero, then only the user data
 * should be flushed, not the meta data.
 *
 * Changed in version 2.2
 */
int bb_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_fsync(path=\"%s\", datasync=%d, fi=0x%08x)\n",
	    path, datasync, fi);
    log_fi(fi);

    if (datasync)
	retstat = fdatasync(fi->fh);
    else
	retstat = fsync(fi->fh);

    if (retstat < 0)
	bb_error("bb_fsync fsync");

    return retstat;
}

/** Set extended attributes */
int bb_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_setxattr(path=\"%s\", name=\"%s\", value=\"%s\", size=%d, flags=0x%08x)\n",
	    path, name, value, size, flags);
    bb_fullpath(fpath, path);

    retstat = lsetxattr(fpath, name, value, size, flags);
    if (retstat < 0)
	retstat = bb_error("bb_setxattr lsetxattr");

    return retstat;
}

/** Get extended attributes */
int bb_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_getxattr(path = \"%s\", name = \"%s\", value = 0x%08x, size = %d)\n",
	    path, name, value, size);
    bb_fullpath(fpath, path);

    retstat = lgetxattr(fpath, name, value, size);
    if (retstat < 0)
	retstat = bb_error("bb_getxattr lgetxattr");
    else
	log_msg("    value = \"%s\"\n", value);

    return retstat;
}

/** List extended attributes */
int bb_listxattr(const char *path, char *list, size_t size)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    char *ptr;

    log_msg("bb_listxattr(path=\"%s\", list=0x%08x, size=%d)\n",
	    path, list, size
	    );
    bb_fullpath(fpath, path);

    retstat = llistxattr(fpath, list, size);
    if (retstat < 0)
	retstat = bb_error("bb_listxattr llistxattr");

    log_msg("    returned attributes (length %d):\n", retstat);
    for (ptr = list; ptr < list + retstat; ptr += strlen(ptr)+1)
	log_msg("    \"%s\"\n", ptr);

    return retstat;
}

/** Remove extended attributes */
int bb_removexattr(const char *path, const char *name)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_removexattr(path=\"%s\", name=\"%s\")\n",
	    path, name);
    bb_fullpath(fpath, path);

    retstat = lremovexattr(fpath, name);
    if (retstat < 0)
	retstat = bb_error("bb_removexattr lrmovexattr");

    return retstat;
}

/** Open directory
 *
 * This method should check if the open operation is permitted for
 * this  directory
 *
 * Introduced in version 2.3
 */
int bb_opendir(const char *path, struct fuse_file_info *fi)
{
    DIR *dp;
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg("\nbb_opendir(path=\"%s\", fi=0x%08x)\n",
      path, fi);
    bb_fullpath(fpath, path);
    
    dp = opendir(fpath);
    if (dp == NULL)
    retstat = bb_error("bb_opendir opendir");
    
    fi->fh = (intptr_t) dp;
    
    log_fi(fi);
    
    return retstat;
}

/** Read directory
 *
 * This supersedes the old getdir() interface.  New applications
 * should use this.
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.  This
 * works just like the old getdir() method.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 *
 * Introduced in version 2.3
 */
int bb_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
           struct fuse_file_info *fi)
{
    int retstat = 0;
    DIR *dp;
    struct dirent *de;
    
    log_msg("\nbb_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
        path, buf, filler, offset, fi);
    // once again, no need for fullpath -- but note that I need to cast fi->fh
    dp = (DIR *) (uintptr_t) fi->fh;

    // Every directory contains at least two entries: . and ..  If my
    // first call to the system readdir() returns NULL I've got an
    // error; near as I can tell, that's the only condition under
    // which I can get an error from readdir()
    de = readdir(dp);
    if (de == 0) {
    retstat = bb_error("bb_readdir readdir");
    return retstat;
    }

    // This will copy the entire directory into the buffer.  The loop exits
    // when either the system readdir() returns NULL, or filler()
    // returns something non-zero.  The first case just means I've
    // read the whole directory; the second means the buffer is full.
    do {
    log_msg("calling filler with name %s\n", de->d_name);
    if (filler(buf, de->d_name, NULL, 0) != 0) {
        log_msg("    ERROR bb_readdir filler:  buffer full");
        return -ENOMEM;
    }
    } while ((de = readdir(dp)) != NULL);
    
    log_fi(fi);
    
    return retstat;
}

/** Release directory
 *
 * Introduced in version 2.3
 */
int bb_releasedir(const char *path, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    log_msg("\nbb_releasedir(path=\"%s\", fi=0x%08x)\n",
        path, fi);
    log_fi(fi);
    
    closedir((DIR *) (uintptr_t) fi->fh);
    
    return retstat;
}

/**
 * Initialize filesystem
 *
 * The return value will passed in the private_data field of
 * fuse_context to all file operations and as a parameter to the
 * destroy() method.
 *
 * Introduced in version 2.3
 * Changed in version 2.6
 */
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
void *bb_init(struct fuse_conn_info *conn)
{
    char * dbpath;
    struct bb_state *bb_data = BB_DATA;

    log_msg("\nbb_init()\n");
    //Abrimos la base de datos 
    dbpath = malloc(sizeof(char)*PATH_MAX);
    bb_fullpath(dbpath,"/.dedupfs/dedupfs.db");
    bb_data->db = db_open(dbpath);
    free(dbpath);
    bb_data->mapopenw = map_open();
    return bb_data;
}

/**
 * Clean up filesystem
 *
 * Called on filesystem exit.
 *
 * Introduced in version 2.3
 */
void bb_destroy(void *userdata)
{
	//Cerramos las bd
    db_close(BB_DATA->db);
    map_close(BB_DATA->mapopenw);
    log_msg("\nbb_destroy(userdata=0x%08x)\n", userdata);
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 *
 * Introduced in version 2.5
 */
int bb_access(const char *path, int mask)
{
    int retstat = 0;
    char fpath[PATH_MAX];

    log_msg("\nbb_access(path=\"%s\", mask=0%o)\n",
        path, mask);
    bb_fullpath(fpath, path);

    retstat = access(fpath, mask);

    if (retstat < 0)
    retstat = bb_error("bb_access access");

    return retstat;
}

/**
 * Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int bb_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    int fd;
    
    log_msg("\nbb_create(path=\"%s\", mode=0%03o, fi=0x%08x)\n",
    		path, mode, fi);
    bb_fullpath(fpath, path);
    
    fd = creat(fpath, mode);
    if (fd < 0)
    	retstat = bb_error("bb_create creat");
    
    fi->fh = fd;
    
    log_fi(fi);
    
    // Se da por supuesto que una llamada a create equivale a abrir
    // para escritura, lo introducimos en el mapa.
    map_add(fi->fh, path, 0, 0);

    return retstat;
}

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 */
int bb_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;

    log_msg("\nbb_ftruncate(path=\"%s\", offset=%lld, fi=0x%08x)\n",
	    path, offset, fi);
    log_fi(fi);

    //Establecer como modificado este descriptor
    map_set_modificado(fi->fh);

    retstat = ftruncate(fi->fh, offset);
    if (retstat < 0)
	retstat = bb_error("bb_ftruncate ftruncate");

    return retstat;
}

/** Change the size of a file */
int bb_truncate(const char *path, off_t newsize)
{
    int retstat = 0;

    log_msg("\nbb_truncate(path=\"%s\", newsize=%lld)\n",
	    path, newsize);

    // Truncate puede verse como una apertura de archivo,
    // truncamiento hasta el tamaño adecuado (modificación),
    // y cierre con todas las implicaciones que tiene el cierre.
    // Se usa bb_open, bb_ftruncate y bb_release para ello.
    struct fuse_file_info *fi = malloc(sizeof(struct fuse_file_info));
    fi->flags = O_RDWR; //Para acceder en escritura
    retstat = bb_open(path, fi);
    if (retstat == 0) {
    	//Se ha abierto adecuadamente, truncamos
    	retstat = bb_ftruncate(path,newsize,fi);
    	if (retstat == 0) {
    		//Se ha truncado adecuadamente, cerramos
    		retstat = bb_release(path, fi);
    	}
    }
    free(fi);

    if (retstat < 0) {
    	bb_error("bb_truncate truncate");
    	close(fi->fh);
    }

    return retstat;
}

/**
 * Get attributes from an open file
 *
 * This method is called instead of the getattr() method if the
 * file information is available.
 *
 * Currently this is only called after the create() method if that
 * is implemented (see above).  Later it may be called for
 * invocations of fstat() too.
 *
 * Introduced in version 2.5
 */
// Since it's currently only called after bb_create(), and bb_create()
// opens the file, I ought to be able to just use the fd and ignore
// the path...
int bb_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
    int retstat = 0;
    
    log_msg("\nbb_fgetattr(path=\"%s\", statbuf=0x%08x, fi=0x%08x)\n",
      path, statbuf, fi);
    log_fi(fi);
    
    //Ahora mismo recibimos el stat del deduplicado.
    //Se puede solucionar llamando a getattr desde aquí

    retstat = fstat(fi->fh, statbuf);
    if (retstat < 0)
    	retstat = bb_error("bb_fgetattr fstat");
    
    log_stat(statbuf);
    
    return retstat;
}

struct fuse_operations bb_oper = {
  .getattr = bb_getattr,
  .readlink = bb_readlink,
  // .getdir = NULL,
  .mknod = bb_mknod,
  .mkdir = bb_mkdir,
  .rmdir = bb_rmdir,
  .symlink = bb_symlink,
  // .rename = bb_rename,
  .link = bb_link,
  .unlink = bb_unlink,
  .chmod = bb_chmod,
  .chown = bb_chown,
  .truncate = bb_truncate,
  .utime = bb_utime,
  .open = bb_open,
  .read = bb_read,
  .write = bb_write,
  .statfs = bb_statfs,
  .flush = bb_flush,
  .release = bb_release,
  .fsync = bb_fsync,
  .setxattr = bb_setxattr,
  .getxattr = bb_getxattr,
  .listxattr = bb_listxattr,
  .removexattr = bb_removexattr,
  .opendir = bb_opendir,
  .readdir = bb_readdir,
  .releasedir = bb_releasedir,
  // .fsyncdir = bb_fsyncdir,
  .init = bb_init,
  .destroy = bb_destroy,
  .access = bb_access,
  .create = bb_create,
  .ftruncate = bb_ftruncate,
  .fgetattr = bb_fgetattr
};

void bb_usage()
{
    fprintf(stderr, "usage:  dedupfs [FUSE and mount options] rootDir mountPoint\n");
    abort();
}

/**
 * Comprobar si existe el directorio .dedupdir en el árbol de directorios
 * subyacente, y crearlo si no existe.
 */
static void check_conf_dir(char rootdir[PATH_MAX]){
	struct stat s;
	char * dedupdir = malloc(sizeof(char) * PATH_MAX);
	strcpy(dedupdir, rootdir);
	strncat(dedupdir, "/.dedupfs", PATH_MAX);
	int err = stat(dedupdir, &s);
	if(-1 == err) {
	    if(ENOENT == errno) {
	        /* No existe, se crea */
	    	mkdir(dedupdir, 0777);
	    	// El de los datos también
	    	strcat(dedupdir,"/data");
	    	mkdir(dedupdir, 0777);
	    } else {
	        perror("Stat error al acceder al directorio \".dedupfs\"");
	        exit(1);
	    }
	} else {
	    if(S_ISDIR(s.st_mode)) {
	        /* Directorio, comprobamos permisos */
	    	if (!(s.st_mode & S_IRWXU)) {
	    		perror("\".dedupfs\" debe tener permisos rwx");
	    		exit(1);
	    	}
	    } else {
	        /* exists but is no dir */
	        perror("\".dedupfs\" debe ser un directorio o no existir");
	        exit(1);
	    }
	}
	free(dedupdir);
}

void maindebug(){
	/*
	fprintf(stderr,"MAIN DEBUG\n");
	char hashstring[41];
	fprintf(stderr,"Calcular hash:\n");
	calcular_hash("/tmp/dedupfsdebug",hashstring, );
	fprintf(stderr,"%s\n", hashstring);
	fprintf(stderr,"OK");
	exit(0);
	*/
}

int main(int argc, char *argv[])
{
    //DEBUG
    //maindebug(); //DEBUG
    //DEBUG
    int fuse_stat;
    struct bb_state *bb_data;

    // asegurarse de que no se ejecuta como root
    if ((getuid() == 0) || (geteuid() == 0)) {
	fprintf(stderr, "Running BBFS as root opens unnacceptable security holes\n");
	return 1;
    }
    
    // Perform some sanity checking on the command line:  make sure
    // there are enough arguments, and that neither of the last two
    // start with a hyphen (this will break if you actually have a
    // rootpoint or mountpoint whose name starts with a hyphen, but so
    // will a zillion other programs)
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-'))
	bb_usage();

    bb_data = malloc(sizeof(struct bb_state));
    if (bb_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the rootdir out of the argument list and save it in my
    // internal data
    bb_data->rootdir = realpath(argv[argc-2], NULL);
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;

    bb_data->logfile = log_open("dedupfs.log");
    
    //Comprobar si .dedupfs existe y que sea legible.
    check_conf_dir(bb_data->rootdir);
    //TODO se puede hacer que check_conf devuelva un valor si existen archivos
    //	en el rootdir, y que dichos archivos se transfieran tras llamar a fuse
    //  para ello, fuse_main debe volver tras la llamada(lo hace?)

    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &bb_oper, bb_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
