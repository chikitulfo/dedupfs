
#define FUSE_USE_VERSION 26


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
#include <openssl/sha.h>

#include "log.h"


struct bb_state {
    FILE *logfile;
    char *rootdir;
};
#define BB_DATA ((struct bb_state *) fuse_get_context()->private_data)

// Report errors to logfile and give -errno to caller
static int bb_error(char *str)
{
    int ret = -errno;
    
    log_msg("    ERROR %s: %s\n", str, strerror(errno));
    
    return ret;
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
    
    log_msg("\nbb_init()\n");
    
    return BB_DATA;
}

struct fuse_operations bb_oper = {
  // .getattr = bb_getattr,
  // .readlink = bb_readlink,
  // .getdir = NULL,
  // .mknod = bb_mknod,
  // .mkdir = bb_mkdir,
  // .unlink = bb_unlink,
  // .rmdir = bb_rmdir,
  // .symlink = bb_symlink,
  // .rename = bb_rename,
  // .link = bb_link,
  // .chmod = bb_chmod,
  // .chown = bb_chown,
  // .truncate = bb_truncate,
  // .utime = bb_utime,
  // .open = bb_open,
  // .read = bb_read,
  // .write = bb_write,
  // .statfs = bb_statfs,
  // .flush = bb_flush,
  // .release = bb_release,
  // .fsync = bb_fsync,
  // .setxattr = bb_setxattr,
  // .getxattr = bb_getxattr,
  // .listxattr = bb_listxattr,
  // .removexattr = bb_removexattr,
  .opendir = bb_opendir,
  .readdir = bb_readdir,
  .releasedir = bb_releasedir,
  // .fsyncdir = bb_fsyncdir,
  .init = bb_init,
  // .destroy = bb_destroy,
  .access = bb_access,
  // .create = bb_create,
  // .ftruncate = bb_ftruncate,
  // .fgetattr = bb_fgetattr
};

void bb_usage()
{
    fprintf(stderr, "usage:  dedupfs [FUSE and mount options] rootDir mountPoint\n");
    abort();
}

int main(int argc, char *argv[])
{
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
    
    bb_data->logfile = log_open();
    
    // turn over control to fuse
    fprintf(stderr, "about to call fuse_main\n");
    fuse_stat = fuse_main(argc, argv, &bb_oper, bb_data);
    fprintf(stderr, "fuse_main returned %d\n", fuse_stat);
    
    return fuse_stat;
}
