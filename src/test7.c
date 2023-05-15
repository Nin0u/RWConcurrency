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
    int r = fork();
    if(r == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if(r == 0) {
        //p1
        printf("P1 : %d\n", getpid());
        rl_descriptor desc1 = rl_open("test.txt", O_RDWR);

        rl_print_open_file(desc1.f);

        struct flock f1;
        f1.l_start = 0;
        f1.l_type = F_WRLCK;
        f1.l_whence = SEEK_SET;
        f1.l_len = 10;
        rl_fcntl(desc1, F_SETLKW, &f1);

        printf("P1 Pose verrou sur F1\n");

        sleep(4);

        rl_descriptor desc2 = rl_open("test.txt", O_RDWR);

        rl_print_open_file(desc2.f);

        struct flock f2;
        f2.l_start = 10;
        f2.l_type = F_WRLCK;
        f2.l_whence = SEEK_SET;
        f2.l_len = 10;

        printf("P1 va poser verrou du F2, mais va bloquer\n");
        rl_fcntl(desc2, F_SETLKW, &f2);
        
        rl_close(desc1);
        rl_close(desc2);


    } else {

        r = fork();

        if(r == 0) {
            //p2
            printf("P2 : %d\n", getpid());

            sleep(1);

            rl_descriptor desc2 = rl_open("test.txt", O_RDWR);

            rl_print_open_file(desc2.f);

            struct flock f2;
            f2.l_start = 10;
            f2.l_type = F_WRLCK;
            f2.l_whence = SEEK_SET;
            f2.l_len = 10;
            rl_fcntl(desc2, F_SETLKW, &f2);

            printf("P2 Pose verrou sur F2\n");

            sleep(4);

            rl_descriptor desc3 = rl_open("test.txt", O_RDWR);

            rl_print_open_file(desc3.f);

            struct flock f3;
            f3.l_start = 20;
            f3.l_type = F_WRLCK;
            f3.l_whence = SEEK_SET;
            f3.l_len = 10;
            printf("P2 va poser un Verrou sur F3, mais va bloquer\n");        
            rl_fcntl(desc3, F_SETLKW, &f3);
            
            rl_close(desc3);
            rl_close(desc2);

        } else {
            printf("P3 : %d\n", getpid());

            sleep(2);

            rl_descriptor desc3 = rl_open("test.txt", O_RDWR);
            rl_print_open_file(desc3.f);

            struct flock f3;
            f3.l_start = 20;
            f3.l_type = F_WRLCK;
            f3.l_whence = SEEK_SET;
            f3.l_len = 10;
            rl_fcntl(desc3, F_SETLKW, &f3);

            printf("P3 Pose verrou sur F3\n");

            sleep(4);

            rl_descriptor desc1 = rl_open("test.txt", O_RDWR);

            rl_print_open_file(desc1.f);

            struct flock f1;
            f1.l_start = 0;
            f1.l_type = F_WRLCK;
            f1.l_whence = SEEK_SET;
            f1.l_len = 10;
            printf("P3 va poser un Verrou sur F1, mais va bloquer et DEADLOCK\n");        
            rl_fcntl(desc1, F_SETLKW, &f1); 

            rl_close(desc3);
            rl_close(desc1);
            printf("FIN\n");  

            while(wait(NULL) != -1);

        }

    }
}