#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "rl_library_lock.h"

int main()
{
    rl_descriptor desc1 = rl_open("test.txt", O_RDWR);

    rl_print_open_file(desc1.f);

    struct flock f1;
    f1.l_start = 0;
    f1.l_type = F_WRLCK;
    f1.l_whence = SEEK_SET;
    f1.l_len = 10;
    rl_fcntl(desc1, F_SETLKW, &f1);
    
    printf("Création du premier lock\n");

    rl_descriptor desc2 = rl_open("test.txt", O_RDWR);

    rl_print_open_file(desc2.f);

    struct flock f2;
    f2.l_start = 5;
    f2.l_type = F_WRLCK;
    f2.l_whence = SEEK_SET;
    f2.l_len = 15;
    rl_fcntl(desc2, F_SETLKW, &f2);

    printf("Création du second lock\n");

    rl_close(desc1);
    rl_close(desc2);

    return 0;
}