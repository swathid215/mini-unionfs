#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include "cow.h"

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

// Dummy resolve (Person 2 will implement)
int resolve_path(const char *path, char *resolved_path) {
    return -1;
}

// getattr
static int unionfs_getattr(const char *path, struct stat *stbuf,
                          struct fuse_file_info *fi) {
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    return -ENOENT;
}

// readdir
static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi,
                          enum fuse_readdir_flags flags) {

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    return 0;
}

// open (CoW integration)  
static int unionfs_open(const char *path, struct fuse_file_info *fi) {
    handle_cow_open(path, fi->flags);
    return 0;

// operations
static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .open = unionfs_open,
};

// main
int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &unionfs_oper, NULL);
}
