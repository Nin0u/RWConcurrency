#ifndef RL_LOCK_H
#define RL_LOCK_H

#include <sys/types.h>
#include "owner.h"

#define NB_OWNERS 20

//Structure représentant un lock

typedef struct rl_lock
{
    // Le prochain lock dans un tableau (une liste sans pointeur) -1 -> dernier, -2 -> case non utilisé
    int next_lock; 
    off_t starting_offset; //a partir de ou
    off_t len; // len == 0 -> tout le fichier
    short type; //F_RDLCK F_WRLCK
    size_t nb_owners; //nb de owners
    owner lock_owners[NB_OWNERS];
    
} rl_lock;


#endif