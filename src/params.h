/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  There are a couple of symbols that need to be #defined before
  #including all the headers.
*/

#ifndef _PARAMS_H_
#define _PARAMS_H_


//No utilizar el log
#define NOLOG
// The FUSE API has been changed a number of times.  So, our code
// needs to define the version of the API that we assume.  As of this
// writing, the most current API version is 26
#define FUSE_USE_VERSION 26

// need this to get pwrite().  I have to use setvbuf() instead of
// setlinebuf() later in consequence.
#define _XOPEN_SOURCE 500

// maintain bbfs state in here
#include <limits.h>
#include <stdio.h>
  
struct bb_state {
    FILE *logfile;
    char *rootdir;
    void *db; // Base de datos que almacena la información de archivos.
    void *mapopenw; // Mapa que gestiona los archivos abiertos para escritura.
};

//Entrada de la base de datos
struct db_entry {
	char path[PATH_MAX];
	char sha1sum[41];
	char datapath[PATH_MAX];
	unsigned int size;
	int deduplicados;
};
//Entrada en el mapa de ficheros abiertos en escritura
struct map_entry {
	unsigned long long int fh;
	char path[PATH_MAX];
	int deduplicados;
	char modificado;
};

#define BB_DATA ((struct bb_state *) fuse_get_context()->private_data)

#endif
