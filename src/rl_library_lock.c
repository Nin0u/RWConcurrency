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

int initialiser_mutex(pthread_mutex_t *pmutex) {
    pthread_mutexattr_t mutexattr;

    if (pthread_mutexattr_init(&mutexattr) != 0) {
        perror("mutexattr init");
        return 1;
    }

    if (pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED) != 0) {
        perror("mutexattr setpshared");
        return 1;
    }

    if (pthread_mutex_init(pmutex, &mutexattr) != 0) {
        perror("mutex init");
        return 1;
    }
    return 0;
}

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

    int first = 0;

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

        first = 1;
    }

    //On lit la mémoire partagée
    rl_open_file *file = mmap(0, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(file == MAP_FAILED)
    {
        perror("mmap");
        return NULL;
    }

    if(first) 
    {
        //Initialise le file !
        file->first = -2;
        for(int i = 0; i < NB_LOCKS; i++)
            file->lock_table[i].next_lock = -2;
        if(initialiser_mutex(&file->mutex_list) != 0)
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
    if(file == NULL) return des;
    des.d = fd;
    des.f = file;
    all_file.tab_open_files[all_file.nb_files] = file;
    all_file.nb_files++;

    return des;
}

static int rl_remove_owner(rl_lock *lock_table, int pos, owner o)
{
    // On supprime de la table !
    for(int i = 0; i < (lock_table + pos)->nb_owners; i++)
    {
        owner oi = (lock_table + pos)->lock_owners[i];
        if((oi.des == o.des) && (oi.proc == o.proc))
        {
            // On shift tous ceux de droite de un cran vers la droite
            for(int j = i + 1; j < (lock_table + pos)->nb_owners - 1; j++)
            {
                (lock_table + pos)->lock_owners[j] = (lock_table + pos)->lock_owners[j + 1];
            }
        }
        (lock_table + pos)->nb_owners--;
        break;
    }

    // On demande à remove dans les autres blocs d'après
    if((lock_table + pos)->next_lock != -1)
        (lock_table + pos)->next_lock = rl_remove_owner(lock_table, (lock_table + pos)->next_lock, o);
    
    // Si on a plus de owner on se supprime, puis on dit que le suivant du bloc d'avant est notre suivant
    if((lock_table + pos)->nb_owners == 0)
    {
        int rep = (lock_table + pos)->next_lock;
        (lock_table + pos)->next_lock = -2;
        return rep;
    }

    return pos;
}

int rl_close( rl_descriptor lfd)
{
    if(close(lfd.d) == -1) return -1;
    if(lfd.f->first == -2) return 0;

    //On vérouille car on va modifier la liste (potentiellement)
    //Comme on vérouille la liste, pas besoin de vérouiller lors du parcours des owners des locks
    pthread_mutex_lock(&lfd.f->mutex_list);

    owner o = {.proc = getpid(), .des = lfd.d};
    lfd.f->first = rl_remove_owner(lfd.f->lock_table, lfd.f->first, o);

    // Cas si y a plus personne dans la liste (la fonction revoie -1 si c'est le cas)
    if(lfd.f->first == -1) lfd.f->first = -2;

    pthread_mutex_unlock(&lfd.f->mutex_list);

    return 0;
}