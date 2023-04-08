#include <stdio.h>

#include "rl_library_lock.h"

int main(void){
    printf("==== Ouverture de test.txt ====\n");
    rl_descriptor desc = rl_open("test.txt", O_CREAT | O_RDWR);
    rl_print_open_file(desc.f);

    printf("==== Ajout d'un verrou =====\n");
    struct flock f;
    f.l_start = 5;
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_CUR;
    f.l_len = 10;
    rl_fcntl(desc, F_SETLKW, &f);
    rl_print_open_file(desc.f);
}