#ifndef RL_OPEN_FILE
#define RL_OPEN_FILE

#include <pthread.h>
#include "rl_lock.h"

#define NB_LOCKS 32

// Contient la liste des locks
// Objet contenu dans un shared_memory_objects
// Trié la liste lock_table en ordre croissant starting_offset

typedef struct rl_open_file
{
    int first;
    rl_lock lock_table[NB_LOCKS];

    char shm[256];

    pthread_mutex_t mutex_list; //Utiliser lorsqu'on modifie la liste !
    pthread_cond_t cond_list;

    int nb_des;
} rl_open_file;


#endif