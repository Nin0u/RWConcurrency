#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>



#include "rl_library_lock.h"

#define NB_FILS 26
#define LEN_BUF 300

int main()
{

    int i = 0;
    for(i = 0; i < NB_FILS; i++) {
        int r = fork();
        if(r == -1) {
            perror("fork");
            exit(1);
        }

        if(r == 0) break;
    }

    rl_descriptor desc = rl_open("TEST.txt", O_CREAT | O_RDWR | O_TRUNC, S_IRWXU);
    //rl_print_open_file(desc.f);


    struct flock f;
    f.l_start = 0;
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_len = LEN_BUF;

    printf("Je lock %d\n", getpid());
    rl_fcntl(desc, F_SETLKW, &f);

    char buf[LEN_BUF];
    for(int j = 0; j < LEN_BUF - 1; j++) {
        buf[j] = 65 + i;
    }
    buf[LEN_BUF - 1] = '\n';
    
    write(desc.d, buf, 300);

    rl_close(desc);
    printf("J'unlock %d\n", getpid());
    wait(NULL);
    return 0;
}