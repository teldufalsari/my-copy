#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#define __USE_ATFILE 1
#include <fcntl.h>

#define BLOCK_SIZE 4096 * 256
extern int optind;
struct copy_options
{
    int force_rewrite;
    int follow_symlinks;
};
static const char* opt_string = "fs";

static struct copy_options opts;

int lstat_file_or_dir(char* name, char** last_name, struct stat* buf, const char* cwd);
void copy_options_init(struct copy_options* x);
int copy (int src_fd, int dst_fd, ssize_t src_size);
static inline int relocate_block (int from_fd, int to_fd, u_int8_t* buf, ssize_t bl_size);
static inline int copy_regular(const char* src_name, const char* dst_name, ssize_t src_size);
static inline int cpy_regular(int src_fd, int  dst_fd, ssize_t src_size);
static inline int copy_fifo(const char* dst_name, mode_t src_mode);
static inline int cpy_fifo(int dstdir_fd, const char* dst_name, mode_t src_mode);
static inline int copy_symlink(const char* src_name, const char* dst_name, ssize_t src_size);
static inline int cpy_symlink(int srcdir_fd, int dstdir_fd, const char* src_name, const char* dst_name, ssize_t src_size);
static inline int copy_device(const char* dst_name, dev_t src_dev_id, mode_t src_dev_mode);
static inline int cpy_device(int dstdir_fd, const char* dst_name, const struct stat* dst_st);
static inline int copy_dir(int src_fd, int dst_fd);
static inline int dot_or_dotdot(const char* name);
int do_copy(int srcdir_fd, const char* src_name, int dstdir_fd, const char* dst_name);
char* get_last(const char* full_name, size_t length);

int main(int argc, char* argv[])
{
    umask(0);
    copy_options_init(&opts);
    int c;
    while ((c = getopt(argc, argv, opt_string)) != -1)
    {
        switch(c)
        {
        case 'l':
            opts.follow_symlinks = 1;
            break;
        case 'f':
            opts.force_rewrite = 1;
            break;
        default:
            //Unreachable
            break;
        }
    }
    if ((argc - optind) < 2)
    {
        printf("Usage: %s [arguments] source_name destination_name\n", argv[0]);
        return 1;
    }
    struct stat st_src;
    if(lstat(argv[1], &st_src))
    {
        perror("stst");
        return 1;
    }
    char cwd[NAME_MAX] = "";
    if (getcwd(cwd, NAME_MAX) == NULL)
    {
        perror("Could not read current directory");
        return 4;
    } 
    int dst_dirfd = 0, src_dirfd = open(cwd, O_DIRECTORY);
    struct stat st_dst;
    char* last_name = NULL;
    int state = lstat_file_or_dir(argv[2], &last_name, &st_dst, cwd);
    if (state < 0)
    {
        perror("Incorrect path");
        puts(argv[2]);
        return 1;
    }
    state += (((st_dst.st_mode & __S_IFMT) == __S_IFDIR) << 1) + (((st_src.st_mode & __S_IFMT) == __S_IFDIR) << 2);
    switch (state)
    {
    case 0:
        dst_dirfd = src_dirfd;
        last_name = argv[2];
        break;

    case 2:
    case 6:
        dst_dirfd = open(argv[2], O_DIRECTORY);
        last_name = get_last(argv[1], strlen(argv[1]));
        break;

    case 3:
        dst_dirfd = open(argv[2], O_DIRECTORY);
        break;
    
    case 7:
        dst_dirfd = open(argv[2], O_DIRECTORY);
        mkdirat(dst_dirfd, last_name, 0700);
        break;

    case 4:
        printf("Cannot copy directory into a file:\n%s to %s\n", argv[1], argv[2]);
        return 1;
    
    default:
        printf("Incorrect path in the filesystem: no such derectory.\n%s\n", argv[2]);
        return 1;
    }
    return do_copy(src_dirfd, argv[1], dst_dirfd, last_name);
}

int copy (int src_fd, int dst_fd, ssize_t size_left)
{ 
    if (size_left >= BLOCK_SIZE)
    {
        u_int8_t* buf = malloc(BLOCK_SIZE);
        while (size_left >= BLOCK_SIZE)
        {
            if (relocate_block(src_fd, dst_fd, buf, BLOCK_SIZE) != 0)
                return 3;
            size_left -= BLOCK_SIZE;
        }
        return relocate_block(src_fd, dst_fd, buf, size_left);
    }
    else
    {
        u_int8_t* buf = malloc(size_left);
        return relocate_block(src_fd, dst_fd, buf, size_left);
    }
}

inline int relocate_block (int from_fd, int to_fd, u_int8_t* buf, ssize_t bl_size)
{
    ssize_t przeczytano = read(from_fd, buf, bl_size);
    if (przeczytano != bl_size)
    {
        perror("Error during reading the source");
        return 1;
    }
    ssize_t written = 0;
    while (written != przeczytano)
        written += write(to_fd, buf + written, bl_size - written);
    return 0;
}

int copy_regular(const char* src_name, const char* dst_name, ssize_t src_size)
{
    int src_fd = open(src_name, O_RDONLY);
    if (src_fd < 0)
    {
        perror("Failed to open the source");
        return 2;
    }
    int dst_fd = open(dst_name, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (dst_fd < 0)
    {
        perror("Failed to open the destination");
        close(src_fd);
        return 2;
    }

    int code;
    ssize_t size_left = src_size;
    if (size_left >= BLOCK_SIZE)
    {
        u_int8_t* buf = malloc(BLOCK_SIZE);
        while (size_left >= BLOCK_SIZE)
        {
            if (relocate_block(src_fd, dst_fd, buf, BLOCK_SIZE) != 0)
                return 3;
            size_left -= BLOCK_SIZE;
        }
        code = relocate_block(src_fd, dst_fd, buf, size_left);
        free(buf);
    }
    else
    {
        u_int8_t* buf = malloc(size_left);
        code = relocate_block(src_fd, dst_fd, buf, size_left);
        free(buf);
    }
    close(src_fd);
    if (close(dst_fd))
    {
        perror("Error durind writing file");
        code = 1;
    }
    return code;
}

int copy_fifo(const char* dst_name, mode_t src_mode)
{
    int code = mkfifo(dst_name, src_mode);
    if (code != 0)
        perror("Error during creating FIFO");
    return code;
}

static inline int cpy_fifo(int dstdir_fd, const char* dst_name, mode_t src_mode)
{
    int code = mkfifoat(dstdir_fd, dst_name, src_mode);
    if (code != 0)
        perror("Error during creating FIFO");
    return code;
}

int copy_symlink(const char* src_name, const char* dst_name, ssize_t src_size)
{
    int code = 0;
    char* buf = malloc(src_size);
    if (src_size != readlink(src_name, buf, src_size))
        {
            printf("Failed to read data from source %s\n", src_name);
            code = 1;
        }
    else
    {
        if (symlink(buf, dst_name) != 0)
        {
        perror("Error during creating a symlink");
        code = 1;
        }
    }
    free(buf);
    return code;
}

static inline int cpy_symlink
(
    int srcdir_fd, int dstdir_fd, 
    const char* src_name, const char* dst_name, 
    ssize_t src_size
)
{
    int code = 0;
    char* buf = malloc(src_size);
    if (src_size != readlinkat(srcdir_fd, src_name, buf, src_size))
        {
            printf("Failed to read data from source %s\n", src_name);
            code = 1;
        }
    else
    {
        if (symlinkat(buf, dstdir_fd, dst_name) != 0)
        {
        perror("Error during creating a symlink");
        code = 1;
        }
    }
    free(buf);
    return code;
}

int copy_device(const char* dst_name, dev_t src_dev_id, mode_t src_dev_mode)
{
    int code = mknod(dst_name, src_dev_mode, src_dev_id);
    if (code != 0)
        perror("Error while copying device file");
    return code;
}

static inline int cpy_device(int dstdir_fd, const char* dst_name, const struct stat* dst_st)
{
    int code = mknodat(dstdir_fd, dst_name, dst_st->st_mode, dst_st->st_dev);
    if (code != 0)
        perror("Error while copying device file");
    return code;
}

static inline int cpy_regular(int src_fd, int  dst_fd, ssize_t src_size)
{
    int code;
    ssize_t size_left = src_size;
    if (size_left >= BLOCK_SIZE)
    {
        u_int8_t* buf = malloc(BLOCK_SIZE);
        while (size_left >= BLOCK_SIZE)
        {
            if (relocate_block(src_fd, dst_fd, buf, BLOCK_SIZE) != 0)
                return 3;
            size_left -= BLOCK_SIZE;
        }
        code = relocate_block(src_fd, dst_fd, buf, size_left);
        free(buf);
    }
    else
    {
        u_int8_t* buf = malloc(size_left);
        code = relocate_block(src_fd, dst_fd, buf, size_left);
        free(buf);
    }

    return code;
}

void copy_options_init(struct copy_options* x)
{
    x->force_rewrite = 0;
    x->follow_symlinks = 0;
}

int lstat_file_or_dir(char* name, char** last_name, struct stat* buf, const char* cwd)
{
    if(lstat(name, buf) == 0) // The path name exists in the filesystem
        return 0;
    
    //If the path does not exist
    size_t offset = strlen(name) - 1;
    while ((name[offset] != '/') && (offset))
        offset--;
    
    if (offset == 0) //The destination file/dir is in cwd and does not exist; should be created
    {
        if (lstat(cwd, buf) != 0)
        {
            free(cwd);
            return -1;
        }
        free(cwd);
        *last_name = name;
        return 1;
    }

    name[offset] = '\0';
    *last_name = name + offset + 1;
    int code = lstat(name, buf);
    if (code == 0)
        return 1;
    return -1;
}

static inline int dot_or_dotdot(const char* name)
{
    if (name[0] == '.')
    {
        char sep = name[(name[1] == '.') + 1];
        return (!sep);
    }
    return 0;
}

int copy_dir(int src_fd, int dst_fd)
{
    DIR* src_dd = fdopendir(src_fd);
    if (src_dd == NULL)
    {
        perror("Could not open directory");
        return 1; 
    }
    
    struct dirent* cur_entry = NULL;
    errno = 0;
    while ((cur_entry = readdir(src_dd)) != NULL)
    {
        if (do_copy(src_fd, cur_entry->d_name, dst_fd, cur_entry->d_name))
            return 1;
        errno = 0;
    }
    if (errno != 0)
    {
        perror ("Error during reading directory");
        return 1;
    }
    return 0;
}

int do_copy(int srcdir_fd, const char* src_name, int dstdir_fd, const char* dst_name)
{
    struct stat src_stat;
    if (fstatat(srcdir_fd, src_name, &src_stat, AT_SYMLINK_NOFOLLOW))
    {
        perror("Could not read source file");
        return 1;
    }

    int code = 0;
    int src_fd, dst_fd;
    switch (src_stat.st_mode & __S_IFMT)
    {
    case __S_IFLNK:
        if (!opts.follow_symlinks)
        {
            code = cpy_symlink(srcdir_fd, dstdir_fd, src_name, dst_name, src_stat.st_size);
            break;
        }
        else 
        {
            //Falling down and copying the file the link is poiting to
        }

    case __S_IFREG:
        src_fd = openat(srcdir_fd, src_name, O_RDONLY);
        if (src_fd < 0)
        {
            perror("Failed to open the source");
            return 2;
        }
        dst_fd = openat(dstdir_fd, dst_name, O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (dst_fd < 0)
        {
            perror("Failed to open destination");
            printf("\t%s at %i\n", dst_name, dstdir_fd);
            close(src_fd);
            return 2;
        }
        code = cpy_regular(src_fd, dst_fd, src_stat.st_size);
        close(src_fd);
        if (close(dst_fd))
        {
            perror("Error durind writing file");
            code = 1;
        }
        break;

    case __S_IFIFO:
        code = cpy_fifo(dstdir_fd, dst_name, src_stat.st_mode);
        break;

    case __S_IFBLK:
    case __S_IFCHR:
        code = cpy_device(dstdir_fd, dst_name, &src_stat);
        break;

    case __S_IFDIR:
        if (dot_or_dotdot(src_name))
            return 0; //Needs to be more informative
        src_fd = openat(srcdir_fd, src_name, __O_DIRECTORY);
        if (src_fd < 0)
        {
            perror("Could not open source directory");
            return 1;
        }
        dst_fd = openat(dstdir_fd, dst_name, __O_DIRECTORY);/// This moment must be overlooked more thoroughly
        if (dst_fd < 0)
        {
            if ((errno == ENOTDIR) && (opts.force_rewrite == 1)) //The file exists but it's not a directory
            {
                if (unlinkat(dstdir_fd, dst_name, 0)) //So we delete it
                    {
                        perror("Could not rewrite file");
                        printf("\t%s\n", dst_name);
                        return 1;
                    }
                if (mkdirat(dstdir_fd, dst_name, 0700)) //And create a directory with the name specified
                {
                    perror("Could not create a directory");
                    printf("\t%s\n", dst_name);
                    return 1;
                }
            }
            else //Possibly there are no files or directories called dst_name
            {
                if (mkdirat(dstdir_fd, dst_name, 0700)) //And create a directory with the name specified
                {
                    perror("Could not create a directory");
                    printf("\t%s\n", dst_name);
                    return 1;
                }
            }
        }
        dst_fd = openat(dstdir_fd, dst_name, __O_DIRECTORY);/// Once again...
        if (dst_fd < 0)
        {
            perror("Could not open the directory for copying");
            printf("\t%s\n", dst_name);
            return 1;
        }
        code = copy_dir(src_fd, dst_fd);
        close(src_fd);
        if (close(dst_fd))
        {
            perror("Error durind writing directory");
            code = 1;
        }
        break;
    default:
        printf("The source is uncopiable\n");
        code = 1;
    }
    return code;
}

char* get_last(const char* full_name, size_t length)
{
    length -= 1;
    if (full_name[length] == '/')
        length--;
    while((full_name[length] != '/') && length)
        length--;
    return full_name + length + 1;
}