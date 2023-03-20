#include "rl_library_lock.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>


#define PREFIX "f"

static rl_all_file all_file;

int rl_init_library()
{
    // Je sais pas trop pour le moment;
    all_file.nb_files = 0;
    return 0;
}

static rl_open_file *open_shm(const char *path)
{
    struct stat st;
    if(stat(path, &st) == -1) 
    {
        perror("stat");
        return NULL;
    }

    // On construit la chaine de caractères
    char name[64];
    memset(name, 0, 64);
    snprintf(name, 64,"/%s_%ld_%ld", PREFIX, st.st_dev, st.st_ino);

    int fd = shm_open(name, O_RDWR | O_EXCL, S_IRWXU | S_IRWXG);

    //Le shm n'existe pas
    if(fd == -1 && errno == ENOENT)
    {
        //On le créer
        fd = shm_open(name, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG);
        if(fd == -1) return NULL;
        if(ftruncate(fd, sizeof(rl_open_file)) == -1)
        {
            perror("ftruncate");
            return NULL;
        }
    }

    //On lit la mémoire partagée
    rl_open_file *file = mmap(0, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(file == MAP_FAILED)
    {
        perror("mmap");
        return NULL;
    }

    return file;
}

//TODO: Ajouter le ... à la signature
// Renvoie {-1, NULL} si pbm
rl_descriptor rl_open(const char *path, int oflag)
{
    rl_descriptor des = {.d = -1, .f = NULL};
    if(all_file.nb_files == NB_FILES) return des;
    int fd = open(path, oflag);
    if(fd == -1)
        perror("open");

    rl_open_file *file = open_shm(path);
    des.d = fd;
    des.f = file;
    all_file.tab_open_files[all_file.nb_files] = file;
    all_file.nb_files++;

    return des;
}