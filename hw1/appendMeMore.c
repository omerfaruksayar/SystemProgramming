#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char const *argv[])
{
    if(argc < 3 || argc > 4)
    {
        //usage message
        return 1;
    }

    else if (argc == 3)
    {   
        //If the file does not exist it will be created.
        int fd = open(argv[1], O_WRONLY | O_APPEND | O_CREAT, 0644);

        if (fd == -1)
        {
            perror("open");
            return 1;   
        }
        
    }

    else{

    }
    
    return 0;
}
