#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>

#include "rl_library_lock.h"

#define NB_PROC 10
#define LEN_TAB 10
#define NB_TRANS 1

void transfert(rl_descriptor desc, int cred, int deb, long somme)
{
    printf("AVANT1 %d\n", getpid());
    //rl_print_open_file(desc.f);
    struct flock f1;
    f1.l_start = cred * sizeof(long);
    f1.l_type = F_WRLCK;
    f1.l_whence = SEEK_CUR;
    f1.l_len = sizeof(long);
    rl_fcntl(desc, F_SETLKW, &f1);
    printf("LOCK1 %d\n", deb);

    struct flock f2;
    f2.l_start = deb * sizeof(long);
    f2.l_type = F_WRLCK;
    f2.l_whence = SEEK_CUR;
    f2.l_len = sizeof(long);

    rl_fcntl(desc, F_SETLKW, &f2);
    printf("LOCK2\n");

    long *soldes = mmap(0, LEN_TAB * sizeof(long), PROT_READ | PROT_WRITE, MAP_SHARED, desc.d, 0);
    if(soldes == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    if(soldes[deb] - somme >= 0) {
        soldes[deb] -= somme;
        soldes[cred] += somme;
        printf("Transfert Réussie !\n");
    }
    munmap(soldes, LEN_TAB * sizeof(long));

    f1.l_type = F_UNLCK;
    f2.l_type = F_UNLCK;
    rl_fcntl(desc, F_SETLK, &f1);
    rl_fcntl(desc, F_SETLK, &f2);
}

int main()
{
    srand(time(NULL));
    int pere = 1;

    rl_descriptor desc = rl_open("compte", O_CREAT | O_RDWR | O_TRUNC, S_IRWXU | S_IRWXG);
    rl_print_open_file(desc.f);

    // Initialisation du fichier de façon random !
    long sum = 0;
    long min = 500;
    long max = 50000;
    for(int i = 0; i < LEN_TAB; i++) {
        long solde = (rand() % (max - min + 1)) + min;
        if(write(desc.d, &solde, sizeof(long)) < sizeof(long)) {
            perror("write init");
            exit(1);
        }
        printf("%ld\n", solde);
        sum += solde;
    }

    printf("HERE\n");
    rl_close(desc);
    // Duplication du processus
    for(int i = 0; i < NB_PROC; i++) {
        int r = fork();
        if(r == -1) {
            perror("fork");
            exit(1);
        }
        if(r == 0) {
            pere = 0;
            break;
        }
    }
    desc = rl_open("compte", O_RDWR);
    // Transactions aléatoire par tout le monde
    for(int i = 0; i < NB_TRANS; i++) {
        int deb = rand() % LEN_TAB;
        int cred = rand() % LEN_TAB;
        cred = 1;
        deb = 6;
        long somme = (rand() % (max - min + 1)) + min;
        transfert(desc, cred, deb, somme);
    }

    if(!pere) rl_close(desc);

    if(pere) {
        int status;
        while(wait(&status) != -1) {
            printf("%d %d\n", WIFEXITED(status), WIFSIGNALED(status));
        }
        perror("wait");
        long verif_sum = 0;
        lseek(desc.d, 0, SEEK_SET);
        for(int i = 0; i < LEN_TAB; i++) {
            long solde;
            int r = read(desc.d, &solde, sizeof(long));
            if(r < sizeof(long)) {
                perror("read final");
                return 1;
            }
            verif_sum += solde;
        }

        printf("TOT avant : %ld\n", sum);
        printf("TOT après : %ld\n", verif_sum);
        rl_print_open_file(desc.f);
        rl_close(desc);
    }

    return 0;
}