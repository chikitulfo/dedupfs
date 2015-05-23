/*
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.
*/

#ifndef _LOG_H_
#define _LOG_H_

#include "params.h"
#include <fuse.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>



//  macro to log fields in structs.
#define log_struct(st, field, format, typecast) \
  log_msg("    " #field " = " #format "\n", typecast st->field)

FILE *log_open(char path[PATH_MAX]);
void log_fi (struct fuse_file_info *fi);
void log_stat(struct stat *si);
void log_statvfs(struct statvfs *sv);
void log_utime(struct utimbuf *buf);

void log_msg(const char *format, ...);
#endif
