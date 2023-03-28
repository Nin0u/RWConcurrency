#ifndef RL_DESC
#define RL_DESC

#include "rl_open_file.h"

// Représente les nouveaux descripteurs

typedef struct rl_descriptor
{
    int d; // Descripteur ede fichier
    rl_open_file *f; // Pointeur vers la mémoire partagée
} rl_descriptor;

#endif