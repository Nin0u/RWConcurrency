#include <stdio.h>

#include "rl_library_lock.h"

int main(void)
{
    printf("Hello World\n");

    rl_descriptor desc = rl_open("test.txt", O_CREAT | O_RDWR);
    rl_print_open_file(desc.f);

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

    rl_print_open_file(desc.f);
    rl_close(desc);

    rl_print_open_file(desc.f);


    return 0;
}