#ifndef COMMON_H
#define COMMON_H

#include <dirent.h>
#include <time.h>

#define MAX_PATH_LENGTH 8192

typedef enum ResponseType {
    OK,
    ERROR
}ResponseType;

typedef struct Info {
    unsigned char type;
    char path[8192];
    char name[8192];
    size_t size;
    int isDeleted;
    time_t lastModified;
} Info;

#endif /* COMMON_H */