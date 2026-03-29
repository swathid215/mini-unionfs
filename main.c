#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

// ---------------- RESOLVE ----------------
int resolve_path(const char *path, char *resolved_path) {
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];

    snprintf(upper_path, sizeof(upper_path), "%s%s", fs->upper_dir, path);
    snprintf(lower_path, sizeof(lower_path), "%s%s", fs->lower_dir, path);

    // Check for whiteout
    char whiteout[PATH_MAX];
    snprintf(whiteout, sizeof(whiteout), "%s%s", fs->upper_dir, path);

    char *base = strrchr(whiteout, '/');
    if (base) {
        char filename[PATH_MAX];
        strcpy(filename, base + 1);

        *(base + 1) = '\0';
        strcat(whiteout, ".wh.");
        strcat(whiteout, filename);
    }

    if (access(whiteout, F_OK) == 0)
        return -1;

    if (access(upper_path, F_OK) == 0) {
        strcpy(resolved_path, upper_path);
        return 0;
    }

    if (access(lower_path, F_OK) == 0) {
        strcpy(resolved_path, lower_path);
        return 0;
    }

    return -1;
}

// ---------------- GETATTR ----------------
static int unionfs_getattr(const char *path, struct stat *stbuf,
                          struct fuse_file_info *fi) {

    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        return 0;
    }

    char resolved[PATH_MAX];

    if (resolve_path(path, resolved) == 0) {
        if (lstat(resolved, stbuf) == -1)
            return -errno;
        return 0;
    }

    return -ENOENT;
}

// ---------------- READDIR ----------------
static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi,
                          enum fuse_readdir_flags flags) {

    struct mini_unionfs_state *fs = UNIONFS_DATA;

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    DIR *dp;
    struct dirent *de;

    // Upper layer first (takes precedence)
    char upper_path[PATH_MAX];
    snprintf(upper_path, sizeof(upper_path), "%s%s", fs->upper_dir, path);

    dp = opendir(upper_path);
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            // Skip .wh.* whiteout files from listing
            if (strncmp(de->d_name, ".wh.", 4) == 0)
                continue;
            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(dp);
    }

    // Lower layer (filtered by whiteouts)
    char lower_path[PATH_MAX];
    snprintf(lower_path, sizeof(lower_path), "%s%s", fs->lower_dir, path);

    dp = opendir(lower_path);
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            char whiteout[PATH_MAX];
            snprintf(whiteout, sizeof(whiteout), "%s%s/%s",
                     fs->upper_dir, path, de->d_name);

            char *base = strrchr(whiteout, '/');
            if (base) {
                char filename[PATH_MAX];
                strcpy(filename, base + 1);

                *(base + 1) = '\0';
                strcat(whiteout, ".wh.");
                strcat(whiteout, filename);
            }

            if (access(whiteout, F_OK) == 0)
                continue;

            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(dp);
    }

    return 0;
}

// ---------------- UNLINK (PERSON 4 - COMPLETE & ROBUST) ----------------
static int unionfs_unlink(const char *path) {
    struct mini_unionfs_state *fs = UNIONFS_DATA;

    char upper_path[PATH_MAX];
    char lower_path[PATH_MAX];
    
    snprintf(upper_path, sizeof(upper_path), "%s%s", fs->upper_dir, path);
    snprintf(lower_path, sizeof(lower_path), "%s%s", fs->lower_dir, path);

    // Case 1: Delete from upper layer if exists
    if (access(upper_path, F_OK) == 0) {
        return unlink(upper_path);
    }

    // Case 2: Create whiteout for lower layer file
    if (access(lower_path, F_OK) == 0) {
        char whiteout[PATH_MAX];
        snprintf(whiteout, sizeof(whiteout), "%s%s", fs->upper_dir, path);
        
        // Convert path to whiteout format: dir/.wh.filename
        char *base = strrchr(whiteout, '/');
        if (base) {
            char filename[PATH_MAX];
            strcpy(filename, base + 1);
            
            *(base + 1) = '\0';  // Null terminate directory path
            strcat(whiteout, ".wh.");
            strcat(whiteout, filename);
        }

        // ✅ PERSON 4 FIX: Create parent directories safely
        char dir_path[PATH_MAX];
        strncpy(dir_path, whiteout, sizeof(dir_path) - 1);
        dir_path[sizeof(dir_path) - 1] = '\0';
        
        char *last_slash = strrchr(dir_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            if (access(dir_path, F_OK) != 0) {
                // Safe mkdir instead of system() - YOUR CRITICAL FIX!
                if (mkdir(dir_path, 0755) == -1 && errno != EEXIST) {
                    return -errno;
                }
            }
        }

        // Create empty whiteout file (touch equivalent)
        FILE *f = fopen(whiteout, "w");
        if (!f) {
            return -errno;
        }
        fclose(f);

        return 0;
    }

    return -ENOENT;
}

// ---------------- OPS ----------------
static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .unlink  = unionfs_unlink,
};

// ---------------- MAIN ----------------
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower> <upper> <mount>\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *fs_data = malloc(sizeof(struct mini_unionfs_state));
    fs_data->lower_dir = realpath(argv[1], NULL);
    fs_data->upper_dir = realpath(argv[2], NULL);

    // 🔥 SHIFT ARGS (VERY IMPORTANT)
    for (int i = 1; i < argc - 2; i++) {
        argv[i] = argv[i + 2];
    }
    argc -= 2;

    return fuse_main(argc, argv, &unionfs_oper, fs_data);
}