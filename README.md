# dedupfs
Deduplication file system using FUSE. Project created using C, FUSE and SQLite to make a basic deduplication file system.

This FUSE based FS is capable of deduplicating identical files on the file system, saving space on it while at the same time being totally transparent to the user, and retaining all the basic working elements of a unix file system (soft links, hard links, permissions, ACLs...)
  - When a new file is added, dedupfs checks its SHA1 hash, and if it is identical to a previously stored file, its space will be deallocated, effectively keeping only one copy of the two files, while virtually keeping one copy for each.
  - When an already deduplicated file is modified in any way, it will detach from the deduplicated file, and the contents of the original deduplicated file will be kept intact.
  
The information about deduplicated files is kept in a database using SQLite.
