#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "rl_library_lock.h"
rl_descriptor desc;

void retire_verrou(int signal){
    struct flock f;
    f.l_start = 5;
    f.l_type = F_UNLCK;
    f.l_whence = SEEK_CUR;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLK, &f);
}

int main(void){
    struct sigaction sa;
    sa.sa_handler = retire_verrou;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    printf("==== Ouverture de test.txt ====\n");
    desc = rl_open("test.txt", O_CREAT | O_RDWR);
    rl_print_open_file(desc.f);

    printf("==== Ajout d'un verrou =====\n");
    struct flock f;
    f.l_start = 5;
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_CUR;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLK, &f);
    rl_print_open_file(desc.f);

    printf("Attente de SIGUSR1 sur %d pour retirer le verrou\n", getpid());
    pause();

    printf("==== Dévérouillage de (5,15) ====\n");
    rl_print_open_file(desc.f);
    
    return EXIT_SUCCESS;
}