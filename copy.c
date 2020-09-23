#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define BLOCK_SIZE 4096 * 256

int copy (int src_fd, int dst_fd, ssize_t src_size);
static inline int relocate_block (int from_fd, int to_fd, u_int8_t* buf, ssize_t bl_size);

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
    
    if ((st_src.st_mode & __S_IFMT) != __S_IFREG)
        {
            printf("The source must be a regular file\n");
            return 1;
        }
    int src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0)
    {
        perror("Failed to open the source");
        return 2;
    }
    int dst_fd = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (dst_fd < 0)
    {
        perror("Failed to open the destination");
        close(src_fd);
        return 2;
    }

    int  ret = copy(src_fd, dst_fd, st_src.st_size);
    close(dst_fd);
    close(src_fd);
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