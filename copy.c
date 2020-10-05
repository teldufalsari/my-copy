#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define BLOCK_SIZE 4096 * 256

int copy (int src_fd, int dst_fd, ssize_t src_size);
static inline int relocate_block (int from_fd, int to_fd, u_int8_t* buf, ssize_t bl_size);
int copy_regular(const char* src_name, const char* dst_name, ssize_t src_size);
int copy_fifo(const char* dst_name, mode_t src_mode);
int copy_symlink(const char* src_name, const char* dst_name, ssize_t src_size);

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s [arguments] source_name destination_name\n", argv[0]);
        return 1;
    }
    struct stat st_src;
    if (lstat(argv[1], &st_src) == -1)
    {
        perror("Failed to read the source");
        return 2;
    }
    umask(0);
    int  ret;
    switch (st_src.st_mode & __S_IFMT)
    {
        case __S_IFREG:
            ret = copy_regular(argv[1], argv[2], st_src.st_size);
            break;
        case __S_IFLNK:
            ret = copy_symlink(argv[1], argv[2], st_src.st_size);
            break;
        case __S_IFIFO:
            ret = copy_fifo(argv[2], st_src.st_mode);
            break;
        default:
            printf("The source is uncopiable\n");
            ret = 1;
    }
    return ret;
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
    }
    else
    {
        u_int8_t* buf = malloc(size_left);
        code = relocate_block(src_fd, dst_fd, buf, size_left);
    }
    close(dst_fd);
    close(src_fd);
    return code;
}

int copy_fifo(const char* dst_name, mode_t src_mode)
{
    int code = mkfifo(dst_name, src_mode);
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
            printf("Failed to read data from source\n");
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
    return code;
}