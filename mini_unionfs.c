#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

/* Global state passed to fuse_main [cite: 37, 44] */
struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define UNIONFS_DATA ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* * Helper: Resolve file location with Whiteout check [cite: 51, 52]
 * Returns: 1 if in upper, 2 if in lower, -ENOENT if not found or whiteouted.
 */
int resolve_path(const char *path, char *resolved)
{
    char whiteout[PATH_MAX];
    char upper[PATH_MAX];
    char lower[PATH_MAX];

    // Get the filename to check for a whiteout prefix [cite: 52]
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    snprintf(whiteout, PATH_MAX, "%s/.wh.%s", UNIONFS_DATA->upper_dir, filename);

    // 1. If whiteout exists, the file is logically deleted [cite: 25, 52]
    if (access(whiteout, F_OK) == 0) {
        return -ENOENT;
    }

    // 2. Check upper directory [cite: 53]
    snprintf(upper, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
    if (access(upper, F_OK) == 0) {
        strcpy(resolved, upper);
        return 1;
    }

    // 3. Check lower directory [cite: 54]
    snprintf(lower, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);
    if (access(lower, F_OK) == 0) {
        strcpy(resolved, lower);
        return 2;
    }

    return -ENOENT;
}

/* Copy file from lower layer to upper layer (CoW) [cite: 21, 69] */
int copy_to_upper(const char *path)
{
    char lower[PATH_MAX], upper[PATH_MAX];
    snprintf(lower, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);
    snprintf(upper, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);

    int src = open(lower, O_RDONLY);
    if (src < 0) return -errno;

    // Preserve original permissions
    struct stat st;
    fstat(src, &st);

    int dest = open(upper, O_CREAT | O_WRONLY | O_TRUNC, st.st_mode);
    if (dest < 0) {
        close(src);
        return -errno;
    }

    char buf[4096];
    ssize_t bytes;
    while ((bytes = read(src, buf, sizeof(buf))) > 0) {
        if (write(dest, buf, bytes) != bytes) break;
    }

    close(src);
    close(dest);
    return 0;
}

static int unionfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    char resolved[PATH_MAX];
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) {
        // Special case for root directory [cite: 63]
        snprintf(resolved, PATH_MAX, "%s", UNIONFS_DATA->upper_dir);
    } else {
        if (resolve_path(path, resolved) < 0) return -ENOENT;
    }

    if (lstat(resolved, stbuf) == -1) return -errno;
    return 0;
}

static int unionfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    DIR *dp;
    struct dirent *de;
    char upper_full[PATH_MAX], lower_full[PATH_MAX];

    snprintf(upper_full, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
    snprintf(lower_full, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    // List upper entries, skipping whiteouts [cite: 17, 18]
    dp = opendir(upper_full);
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            if (strncmp(de->d_name, ".wh.", 4) == 0 || strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(dp);
    }

    // List lower entries if not masked [cite: 25]
    dp = opendir(lower_full);
    if (dp) {
        while ((de = readdir(dp)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;

            char wh_check[PATH_MAX], up_check[PATH_MAX];
            snprintf(wh_check, PATH_MAX, "%s/.wh.%s", upper_full, de->d_name);
            snprintf(up_check, PATH_MAX, "%s/%s", upper_full, de->d_name);

            // Hide if whiteouted or if it exists in the upper layer [cite: 18, 25]
            if (access(wh_check, F_OK) == 0 || access(up_check, F_OK) == 0)
                continue;

            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(dp);
    }
    return 0;
}

static int unionfs_open(const char *path, struct fuse_file_info *fi)
{
    char resolved[PATH_MAX];
    int loc = resolve_path(path, resolved);
    if (loc < 0) return -ENOENT;

    // Trigger CoW if opened for writing and file is only in lower [cite: 62, 68, 69]
    if ((fi->flags & (O_WRONLY | O_RDWR)) && loc == 2) {
        int res = copy_to_upper(path);
        if (res < 0) return res;
        snprintf(resolved, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
    }

    int fd = open(resolved, fi->flags);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int unionfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1) return -errno;
    return res;
}

static int unionfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int res = pwrite(fi->fh, buf, size, offset);
    if (res == -1) return -errno;
    return res;
}

static int unionfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    char full[PATH_MAX];
    snprintf(full, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
    
    // If a whiteout existed for this name, remove it first [cite: 24]
    char whiteout[PATH_MAX];
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;
    snprintf(whiteout, PATH_MAX, "%s/.wh.%s", UNIONFS_DATA->upper_dir, filename);
    unlink(whiteout);

    int fd = open(full, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int unionfs_unlink(const char *path)
{
    char upper[PATH_MAX], lower[PATH_MAX], whiteout[PATH_MAX];
    snprintf(upper, PATH_MAX, "%s%s", UNIONFS_DATA->upper_dir, path);
    snprintf(lower, PATH_MAX, "%s%s", UNIONFS_DATA->lower_dir, path);

    if (access(upper, F_OK) == 0) {
        return unlink(upper); // Physical delete from upper [cite: 75]
    }

    if (access(lower, F_OK) == 0) {
        // Create whiteout to hide file from lower [cite: 24, 76]
        const char *filename = strrchr(path, '/');
        filename = filename ? filename + 1 : path;
        snprintf(whiteout, PATH_MAX, "%s/.wh.%s", UNIONFS_DATA->upper_dir, filename);
        int fd = open(whiteout, O_CREAT, 0644);
        if (fd >= 0) close(fd);
        return 0;
    }
    return -ENOENT;
}

static int unionfs_release(const char *path, struct fuse_file_info *fi)
{
    close(fi->fh);
    return 0;
}

static struct fuse_operations unionfs_oper = {
    .getattr = unionfs_getattr,
    .readdir = unionfs_readdir,
    .open    = unionfs_open,
    .read    = unionfs_read,
    .write   = unionfs_write,
    .create  = unionfs_create,
    .unlink  = unionfs_unlink,
    .release = unionfs_release,
};

int main(int argc, char *argv[])
{
    static struct mini_unionfs_state state;
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <lower_dir> <upper_dir> <mountpoint> [fuse_options]\n", argv[0]);
        return 1;
    }

    // Resolve absolute paths for the directories [cite: 37]
    state.lower_dir = realpath(argv[1], NULL);
    state.upper_dir = realpath(argv[2], NULL);

    // Shift arguments to hide lower/upper from FUSE [cite: 97]
    argv[1] = argv[3];
    for (int i = 2; i < argc - 1; i++) argv[i] = argv[i+1];

    return fuse_main(argc - 2, argv, &unionfs_oper, &state);
}
