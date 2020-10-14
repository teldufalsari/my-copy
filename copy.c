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
#define ABS_PATH 0
extern int optind;
static struct copy_options
{
    int force_rewrite;
    int follow_symlinks;
};
static const char* opt_string = "fs";

static struct copy_options opts;

int lstat_file_or_dir(char* name, struct stat* buf);
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
int do_copy(int src_fd, const char* src_name, int dst_fd, const char* dst_name);

int main(int argc, char* argv[])
{
    umask(0);
    copy_options_init(&opts);
    int c, code;
    while ((c = getopt(argc, argv, opt_string)) != -1)
    {
        switch(c)
        {
            case 'l':
                opts.follow_symlinks = 1;
                break;
            case 'f':
                opts.force_rewrite = 1;
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
    struct stat st_dst;
    int is_src_dir, state = lstat_file_or_dir(argv[2], &st_dst);
    
    if (state < 0) //The path is totally incorrect
    {
        printf("Error during reading the destination: No such file or directory\n");
        return 1;
    }
    else if (state == 1) //The path is probably correct, but destination file does not exist
    {
        if ((st_dst.st_mode & __S_IFMT) == __S_IFDIR)
            is_src_dir = 1;
        else
        {
            printf("Error during reading the destination: incorrect path in the filesystem\n");
            return 1;
        }
    }
    else if ((st_dst.st_mode & __S_IFMT) == __S_IFDIR) //The path is correct
        is_src_dir = 1; 
    else
        is_src_dir = 0;
    
    struct stat st_src;
    if (lstat(argv[1], &st_src) == -1)
    {
        perror("Failed to read the source");
        return 2;
    }
    switch (st_src.st_mode & __S_IFMT)
    {
        case __S_IFREG:
            code = copy_regular(argv[1], argv[2], st_src.st_size);
            break;
        case __S_IFLNK:
            if (opts.follow_symlinks)
                code = copy_regular(argv[1], argv[2], st_src.st_size);
            else
                code = copy_symlink(argv[1], argv[2], st_src.st_size);
            break;
        case __S_IFIFO:
            code = copy_fifo(argv[2], st_src.st_mode);
            break;
        case __S_IFBLK:
        case __S_IFCHR:
            code = copy_device(argv[2], st_src.st_dev, st_src.st_mode);
            break;
        case __S_IFDIR:
            if (!is_src_dir)
                {
                    printf("Error: a directory cannot be copied into a file.");
                    exit(1);
                }
            if (dot_or_dotdot(argv[1]))
                return 0; //Needs to be more informative
            int src_fd = openat(ABS_PATH, argv[1], __O_DIRECTORY | O_RDONLY);
            if (src_fd < 0)
            {
                perror("Could not open source directory");
                printf("\t%s\n", argv[1]);
                return 1;
            }
            int dst_fd = openat(ABS_PATH, argv[2], __O_DIRECTORY);
            if (dst_fd < 0)
            {
                perror("Could not open destination directory");
                printf("\t%s\n", argv[2]);
                return 1;
            }
            code = copy_dir(src_fd, dst_fd);
            break;

        default:
            printf("The source is uncopiable\n");
            code = 1;
    }
    return code;
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

int lstat_file_or_dir(char* name, struct stat* buf)
{
    int code = lstat(name, buf);
    if (code == 0)
        return code;
    
    size_t offset = strlen(name) - 1;
    while (name[offset] != '/')
        --offset;
    name[offset] = '0';
    code = lstat(name, buf);
    name[offset] = '/';
    if (code == 0)
        return 1;
    return code;
}

static inline int dot_or_dotdot(const char* name)
{
    if (name[0] == '.')
    {
        char sep = name[(name[1] == '.') + 1];
        return ((!sep) || (sep == '/'));
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
    printf("dstdir = (%i)\tdstname = '%s'\n", dstdir_fd, dst_name);
    struct stat src_stat;
    if (fstatat(srcdir_fd, src_name, &src_stat, AT_SYMLINK_NOFOLLOW))
    {
        perror("Could not read source file");
        return 1;
    }

    int code = 0;
    switch (src_stat.st_mode & __S_IFMT)
    {
        int src_fd, dst_fd;
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
                perror("Failed to open the destination");
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
            src_fd = openat(srcdir_fd, src_name, __O_DIRECTORY | O_RDONLY);
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