#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "rl_library_lock.h"

int main(void)
{
    printf("Hello World\n");

    rl_descriptor desc = rl_open("test.txt", O_CREAT | O_RDWR);

    rl_print_open_file(desc.f);
    printf("COUCOU 1\n");
    struct flock f;
    f.l_start = 0;
    f.l_type = F_RDLCK;
    f.l_whence = SEEK_CUR;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLK, &f);
    printf("COUCOU 2\n");

    f.l_start = 10;
    f.l_type = F_RDLCK;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLK, &f);    
    printf("COUCOU 3\n");

    f.l_start = 5;
    f.l_len = 10;
    f.l_type = F_RDLCK;
    rl_fcntl(desc, F_SETLK, &f);
    printf("COUCOU 4\n");

    rl_print_open_file(desc.f);


    rl_fcntl(desc, F_UNLCK, &f);
    printf("COUCOU 5\n");

    rl_print_open_file(desc.f);


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
    switch(rl_fork()) {
        case 0 :
            rl_print_open_file(desc.f);
            printf("=========================\n\n");
            break;
        case -2:
            printf("limite de nb_owners atteinte\n");
            break;
        case -3:
            printf("lock error\n");
            break;
        case -4:
            printf("unlock error\n");
            break;
        default :
            printf("Retour du parent\n");
            break;
    }

    
    rl_print_open_file(desc.f);
    wait(NULL);

    //rl_close(d2);
    //rl_close(d);
    //rl_close(desc);
    rl_print_open_file(desc.f);

    return 0;
}