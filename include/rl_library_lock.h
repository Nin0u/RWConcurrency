#ifndef RL_LIBRARY_LOCK
#define RL_LIBRARY_LOCK

#include <sys/file.h>

#include "rl_all_file.h"
#include "rl_descriptor.h"

int rl_init_library();

rl_descriptor rl_open(const char *path, int oflag);
int rl_close( rl_descriptor lfd);

int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck);

rl_descriptor rl_dup( rl_descriptor lfd );
rl_descriptor rl_dup2( rl_descriptor lfd, int newd );

pid_t rl_fork();

void rl_print_open_file(rl_open_file *f);
void rl_print_lock_tab(rl_lock *lock, int first);

#endif