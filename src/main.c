#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "rl_library_lock.h"

int main(void)
{
    printf("Hello World\n");

    printf("======= ETAT INITIAL =======\n");
    rl_descriptor desc = rl_open("test.txt", O_CREAT | O_RDWR);
    rl_print_open_file(desc.f);
    printf("============================\n");

    printf("\n====== AJOUT DE 3 VERROUS ======\n");
    
    printf("(WRLCK de 0 à 10) ");
    struct flock f;
    f.l_start = 0;
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_CUR;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLK, &f);

    printf("(WRLCK de 10 à 20) ");
    f.l_start = 10;
    f.l_type = F_WRLCK;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLK, &f);    

    printf("(WRLCK de 5 à 15)\n");
    f.l_start = 5;
    f.l_len = 10;
    f.l_type = F_WRLCK;
    rl_fcntl(desc, F_SETLK, &f);

    printf("---- Resultat ----\n");
    rl_print_open_file(desc.f);

    printf("\n---- Dévérouillage de (5,15) ---- \n");
    f.l_type = F_UNLCK;
    rl_fcntl(desc, F_SETLK, &f);

    rl_print_open_file(desc.f);

    printf("============================\n");


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
    printf("=========================\n\n");

    printf("\n======= TEST CLOSE =======\n");
    printf("---- Avant close ----\n");
    wait(NULL);
    rl_print_open_file(desc.f);

    rl_close(d2);
    rl_close(d);
    rl_close(desc);

    printf("---- Apres close ----\n");
    rl_print_open_file(desc.f);

    return 0;
}