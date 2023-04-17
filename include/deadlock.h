#ifndef DEADLOCK_H
#define DEADLOCK_H

#define MAX_NB  256
#define MAX_LEN_SHM 256
#include "owner.h"
#include <fcntl.h>

typedef struct deadlock
{
    owner o[MAX_NB];
    struct flock lck[MAX_NB];
    char shm[MAX_NB][MAX_LEN_SHM];
    int nb;

    pthread_mutex_t mutex;
} deadlock;


#endif