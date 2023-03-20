#ifndef OWNER_H
#define OWNER_H

#include <sys/types.h>

// Structure représentant un pocesseur de fichier

typedef struct owner
{
    pid_t proc; // pid du processus
    int des;  // descripteur du fichier
} owner;


#endif