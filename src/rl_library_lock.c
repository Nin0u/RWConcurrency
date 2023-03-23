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

    //TODO: Pour le SETLKW -> réveiller les gens bloquer par la cond !
    return 0;
}

static int rl_find(rl_lock *lock_table, int pos, struct flock *lck)
{
    rl_lock *current = lock_table + pos;
    if(current->len == lck->l_len && current->starting_offset == lck->l_start) return pos;
    if((lock_table + pos)->next_lock == -1 || lck->l_start > current->starting_offset) return -2;
    return rl_find(lock_table, current->next_lock, lck);
}

static int add_pos(rl_lock *lock_table, int pos, struct flock *lck, owner o)
{
    int i = 0;
    for(i = 0; i < NB_LOCKS; i++)
        if(lock_table[i].next_lock == -2) break;
    if(i == NB_LOCKS) return -2;

    lock_table[i].len = lck->l_len;
    lock_table[i].lock_owners[0] = o;
    lock_table[i].starting_offset = lck->l_start;
    lock_table[i].type = lck->l_type;
    lock_table[i].next_lock = pos;
    return i;
}

// -2 -> no place
// -3 -> overlap
// len indique jusqu'où va le précédent lock
// prev_type indique le type du précédent lock (write or read)
static int rl_add(rl_lock *lock_table, int pos, struct flock *lck, owner o, int len, short prev_type)
{
    // Si overlap par la gauche
    //! normalement le strict est bon, mais à vérifier
    if(lck->l_start < len)
    {
        // Si un des 2 est un write on quitte !
        if(prev_type == F_WRLCK) return -3;
        if(lck->l_type == F_WRLCK) return -3;
    }

    rl_lock *current = lock_table + pos;

    // On essaie de add le lock
    if(lck->l_start <= current->starting_offset)
    {

        //Si overlap par la droite
        if((lck->l_len == 0) || (lck->l_start + lck->l_len > current->starting_offset))
        {
            if(lck->l_type == F_WRLCK) return -3;
            if(current->next_lock == F_WRLCK) return -3;
        }

        // -2 si pas de place
        return add_pos(lock_table, pos, lck, o);
    }

    // Pour l'ajout à la fin
    if(current->next_lock == -1)
    {
        // Overlap
        if(current->len == 0 || current->len + current->starting_offset > lck->l_start)
        {
            if(current->type == F_WRLCK) return -3;
            if(lck->l_type == F_WRLCK) return -3;
        }

        // -2 si pas de place
        return add_pos(lock_table, pos, lck, o);
    }

    // On passe au prochain 
    int next = rl_add(lock_table, current->next_lock, lck, o, current->starting_offset + (current->len == 0 ? lck->l_start + 1 : current->len), current->type);
    if(next <= -2) return next;
    current->next_lock = next;
    return pos;
}

static int is_in_lock(rl_lock *lck, owner o)
{
    for(int i = 0; i < lck->nb_owners; i++)
    {
        owner oi = lck->lock_owners[i];
        if(oi.des == o.des && oi.proc == o.proc) return 1;
    }
    return 0;
}

static int rl_unlock(rl_lock *lock_table, int pos, struct flock *lck, owner o, int state)
{
    rl_lock *current = lock_table + pos;
    
    // Si on trouve lok avec lui alors on coupe à cet endroit
    if(current->starting_offset <= lck->l_start && state == 0 && is_in_lock(current, o))
    {
        state = 1;
        //On doit add un lock avec tous les autres dedans plus court
        if(current->starting_offset < lck->l_start)
        {
            
        }
    }
}

int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck)
{
    if(cmd != F_SETLK && cmd != F_SETLKW) return -1;
    //TODO: Pour plus tard : Si F_SETLKW alors vérifier qu'on peut mettre le lck, sinon on wait sur le cond
    
    owner o = {.proc = getpid(), .des = lfd.d};

    pthread_mutex_lock(&lfd.f->mutex_list);

    //TODO: si y a des processus propriétaire de lck non vivants, on les enlèves, on clean quoi !

    if(lck->l_type == F_UNLCK)
    {


        pthread_mutex_unlock(&lfd.f->mutex_list);
        return 0;
    }

    int pos = rl_find(lfd.f->lock_table, lfd.f->first, lck);

    //! Si ce n'est pas un F_UNLCK
    // Si n'est pas dans les lck, on l'ajoute
    if(pos == -2)
    {
        //Les read peuvent s'overlapper
        //Les write - read / write - write peuvent pas
        //! Incompréhension du sujet :
        //!     Si La pose de verrou est possible si et seulement si aucun verrou incompatible 
        //!     posé sur une partie du segment n’a de propriétaire différent de lfd_owner
        //!    
        //!     Par sur de comprendre
        //!     Surement devoir changer ça du coup
        pos = rl_add(lfd.f->lock_table, lfd.f->first, lck, o, 0, -1);
        if(pos == -2)
        {
            printf("Overlapp\n");
            //TODO: errno
            pthread_mutex_unlock(&lfd.f->mutex_list);
            return -1;
        }
        if(pos == -3)
        {
            printf("No Place\n");
            //TODO: errno
            pthread_mutex_unlock(&lfd.f->mutex_list);
            return -1;
        }
        lfd.f->first = pos;
    } else {
        // TODO: COMPLIQUE
        //Si il n est pas proprietaire dans le verrou
            // Si il est de meme type on l'ajoute
            // Sinon on refuse....
        
        //Si il est propriétaire
            // Si c'est le meme -> rien à faire
            // Si il le change pour un READ ---> 
                // si y a d autre gens on stop 
                // sinon on change
            // Si il le change pour un WRITE --->
                // si y a d autre gens on peut PAS pour moi ! (PAS d apres le sujet)
                // sinon on change

    }

    pthread_mutex_unlock(&lfd.f->mutex_list);
    return 0;
}