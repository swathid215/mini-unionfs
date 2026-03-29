#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#include "cow.h"

#define UPPER_DIR "upper"
#define LOWER_DIR "lower"

void copy_file(const char *src, const char *dest) {
    int in = open(src, O_RDONLY);
    int out = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (in < 0 || out < 0) {
        perror("File open failed");
        return;
    }

    char buf[1024];
    int bytes;

    while ((bytes = read(in, buf, sizeof(buf))) > 0) {
        write(out, buf, bytes);
    }

    close(in);
    close(out);
}

int handle_cow_open(const char *path, int flags) {
    char lower_path[256], upper_path[256];

    sprintf(lower_path, "%s%s", LOWER_DIR, path);
    sprintf(upper_path, "%s%s", UPPER_DIR, path);

    if (flags & O_WRONLY || flags & O_RDWR) {
        if (access(upper_path, F_OK) != 0 &&
            access(lower_path, F_OK) == 0) {

            printf("Copying file from lower to upper: %s\n", path);

            copy_file(lower_path, upper_path);
        }
    }

    return 0;
}