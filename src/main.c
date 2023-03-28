#include <stdio.h>

#include "rl_library_lock.h"

int main(void)
{
    printf("Hello World\n");

    rl_descriptor desc = rl_open("test.txt", O_CREAT | O_RDWR);

    struct flock f;
    f.l_start = 0;
    f.l_type = F_RDLCK;
    f.l_whence = SEEK_CUR;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLK, &f);

    f.l_start = 10;
    f.l_type = F_RDLCK;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLK, &f);    

    f.l_start = 5;
    f.l_len = 10;
    f.l_type = F_RDLCK;
    rl_fcntl(desc, F_SETLK, &f);

    rl_fcntl(desc, F_UNLCK, &f);

    printf("\n======= TEST DUP =======\n");
    printf("---- Avant dup ----\n");
    rl_print_open_file(desc.f);
    rl_descriptor d = rl_dup(desc);
    printf("---- Après dup ----\n");
    rl_print_open_file(d.f);
    rl_descriptor d2 = rl_dup2(desc,10);
    printf("---- Apres dup2 sur 10 ----\n");
    rl_print_open_file(d2.f);
    printf("========================\n");

    printf("\n======= TEST FORK =======\n");
    if(rl_fork() == 0) {
        rl_print_open_file(d2.f);
        printf("=========================\n\n");
    }

    rl_print_open_file(desc.f);
    rl_close(desc);

    return 0;
}