#ifndef RL_ALL_FILE
#define RL_ALL_FILE

#include "rl_open_file.h"

// A stocker statiquement !
// Contient tous les rl_open_file utilisé par le processus !
// Utile pour le fork()

#define NB_FILES 256
typedef struct rl_all_file
{
    int nb_files;
    rl_open_file *tab_open_files[256];
} rl_all_file;


#endif