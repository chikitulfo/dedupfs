
#define FUSE_USE_VERSION 26


#include <fuse.h>
#include <limits.h>
#include <stdlib.h>
#include <openssl/sha.h>

#include "log.h"


struct dedup_state {
    FILE *logfile;
    char *rootdir;
};
#define BB_DATA ((struct dedup_state *) fuse_get_context()->private_data)


struct fuse_operations dedup_oper = {
  // .getattr = dedup_getattr,
  // .readlink = dedup_readlink,
  // .getdir = NULL,
  // .mknod = dedup_mknod,
  // .mkdir = dedup_mkdir,
  // .unlink = dedup_unlink,
  // .rmdir = dedup_rmdir,
  // .symlink = dedup_symlink,
  // .rename = dedup_rename,
  // .link = dedup_link,
  // .chmod = dedup_chmod,
  // .chown = dedup_chown,
  // .truncate = dedup_truncate,
  // .utime = dedup_utime,
  // .open = dedup_open,
  // .read = dedup_read,
  // .write = dedup_write,
  // .statfs = dedup_statfs,
  // .flush = dedup_flush,
  // .release = dedup_release,
  // .fsync = dedup_fsync,
  // .setxattr = dedup_setxattr,
  // .getxattr = dedup_getxattr,
  // .listxattr = dedup_listxattr,
  // .removexattr = dedup_removexattr,
  // .opendir = dedup_opendir,
  // .readdir = dedup_readdir,
  // .releasedir = dedup_releasedir,
  // .fsyncdir = dedup_fsyncdir,
  // .init = dedup_init,
  // .destroy = dedup_destroy,
  // .access = dedup_access,
  // .create = dedup_create,
  // .ftruncate = dedup_ftruncate,
  // .fgetattr = dedup_fgetattr
};

void dedup_usage()
{
    fprintf(stderr, "usage:  dedupfs [FUSE and mount options] rootDir mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct dedup_state *dedup_data;

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
	dedup_usage();

    dedup_data = malloc(sizeof(struct dedup_state));
    if (dedup_data == NULL) {
	perror("main calloc");
	abort();
    }

    // Pull the rootdir out of the argument list and save it in my
    // internal data
    dedup_data->rootdir = realpath(argv[argc-2], NULL);
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    dedup_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &dedup_oper, dedup_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
