#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int dup(int oldfd){
    int newfd = fcntl(oldfd, F_DUPFD, 0);
    if(newfd == -1){
        return -1;
    }
    return newfd;
}

int dup2(int oldfd, int newfd){

    //If oldfd equals newfd, then dup2() returns newfd without closing it first.
    if(oldfd == newfd){
        return newfd;
    }

    //If oldfd is not a valid file descriptor, then the call fails, and newfd is not closed. Errno set to EBADF.
    if(fcntl(oldfd, F_GETFL) == -1){
        errno = EBADF;
        return -1;
    }

    //If newfd is a valid file descriptor, then it is silently closed before being reused.
    if(fcntl(newfd, F_GETFL) != -1){
        close(newfd);
    }
    
    int fd = fcntl(oldfd, F_DUPFD, newfd);
    if(fd == -1){
        return -1;
    }
    return fd;
}

int main(int argc, char const *argv[])
{
    int fd = open("test.txt", O_RDWR | O_CREAT, 0666);
    int fd2 = dup(fd);
    int fd3 = dup2(fd2, 10);

    if (dup2(-1,fd3) == -1)
        perror("dup2");
    
    printf("\nCurrent fds are\nfd: %d, fd2: %d, fd3: %d\n\n", fd, fd2, fd3);

    int seekOffset = lseek(fd, 0, SEEK_CUR);
    printf("File offsets before the write operation\n\n");
    printf("File offset of the fd (%d) : %d\n",fd,seekOffset);
    seekOffset = lseek(fd2, 0, SEEK_CUR);
    printf("File offset of the fd2 (%d) : %d\n",fd2,seekOffset);
    seekOffset = lseek(fd3, 0 ,SEEK_CUR);
    printf("File offset of the fd3 (%d) : %d\n\n",fd3,seekOffset);

    const char* str = "Hello World!";
    int check = write(fd, str, 12);
    if(check == -1){
        perror("write");
        return 1;
    }
    printf("File offsets after the write operation\n\n");
    seekOffset = lseek(fd, 0, SEEK_CUR);
    printf("File offset of the fd (%d) : %d\n",fd,seekOffset);
    seekOffset = lseek(fd2, 0, SEEK_CUR);
    printf("File offset of the fd2 (%d) : %d\n",fd2,seekOffset);
    seekOffset = lseek(fd3, 0 ,SEEK_CUR);
    printf("File offset of the fd3 (%d) : %d\n\n",fd3,seekOffset);

    lseek(fd3, 0, SEEK_SET); //sets the file offset to beginning of the file

    char* buf = (char*)malloc(sizeof(char)*12);
    check = read(fd2, buf, 12);
    if(check == -1){
        perror("read");
        return 1;
    }
    printf("Written string is: %s\n", buf);
    free(buf);
    close(fd);
    return 0;
}