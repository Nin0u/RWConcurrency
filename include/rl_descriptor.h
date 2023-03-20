#ifndef RL_DESC
#define RL_DESC

#include "rl_open_file.h"

// Représente les nouveaux descripteurs

typedef struct rl_descriptor
{
    int d;
    rl_open_file *f;
} rl_descriptor;

#endif