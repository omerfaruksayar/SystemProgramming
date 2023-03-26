#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void usage(){
	fprintf(stderr, "Usage: appendMeMore filename num-bytes [x]\n");
}

void writeToFile(const char* path, int numBytes, int isSupplied){
    int fd;
    char byte = 'a';

    /*The  file is opened in append mode.  Before each write(2), the file offset is positioned at the end of the
    file, as if with lseek(2).  The modification of the file offset and the write operation are performed as a
    single atomic step */
    if(!isSupplied)
    {   
        //If the file does not exist it will be created.
        fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (fd == -1)
        {
            perror("open");
            exit(EXIT_FAILURE);   
        }

        for (size_t i = 0; i < numBytes; i++)
        {
            int bytesWritten = write(fd, &byte, sizeof(char));
            if (bytesWritten == -1)
            {
                perror("write");
                exit(EXIT_FAILURE);
            }
        }
    }

    else{

        //If the file does not exist it will be created and opened without O_APPEND flag.
        fd = open(path, O_WRONLY | O_CREAT, 0644);

        if (fd == -1)
        {
            perror("open");
            exit(EXIT_FAILURE);   
        }

        for (size_t i = 0; i < numBytes; i++)
        {   
            lseek(fd, 0, SEEK_END); //move the file pointer to the end of the file
            int bytesWritten = write(fd, &byte, sizeof(char));
            if (bytesWritten == -1)
            {
                perror("write");
                exit(EXIT_FAILURE);
            }
        }
    }
    
    close(fd);    
}

int main(int argc, char const *argv[])
{
    if(argc < 3 || argc > 4)
    {
        usage();
        return 1;
    }

    else if (argc == 3)
        writeToFile(argv[1], atoi(argv[2]),0);

    else
        writeToFile(argv[1], atoi(argv[2]),1);
    
    return 0;
}