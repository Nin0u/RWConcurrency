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

static void remove_owner_in_lock(rl_lock *lock_table, int pos, owner o)
{
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


//! Peut être ne regarder que les write qui sont différent de o
static int get_min_start_write(rl_lock *lock_table, int pos, owner o)
{
    rl_lock *current = lock_table + pos;
    if(current->next_lock == -1 && current->type == F_WRLCK) return current->starting_offset;
    if(current->type == F_WRLCK) return current->starting_offset;
    if(current->next_lock == -1) return -1;
    int next_min = get_min_start_write(lock_table, current->next_lock, o);
    return next_min;
}


// -2 -> no place
// -3 -> overlap
// max_wrlen indique le max de la borne gauche d'un lock write (pour les read seule les write nous embetent)
//! lors de la vérification de l'overlap, surement qu'il faut aussi vérifier si y a le owner ou pas
//! Si y a le owner, on s'en fout de l'overlap et sinon faut faire gaffe
static int rl_add_readlck(rl_lock *lock_table, int pos, struct flock *lck, owner o, int max_wrlen)
{

    //Overlap par le gauche !
    if(max_wrlen > lck->l_start) return -3;

    rl_lock *current = lock_table + pos;

    if(lck->l_start <= current->starting_offset)
    {
        int min_wrstart = get_min_start_write(lock_table, pos, o);
        if(min_wrstart != -1 && lck->l_len == 0) return -3;
        if(min_wrstart != -1 && min_wrstart < lck->l_start + lck->l_len) return -3;

        // -2 si pas de place
        return add_pos(lock_table, pos, lck, o);
    }    

    // Ajout à la fin !
    if(current->next_lock == -1)
    {
        //Si overlap
        if(current->type == F_WRLCK && (current->len == 0 || current->len + current->starting_offset > lck->l_start))
            return -3;
        
        current->next_lock = add_pos(lock_table, -1, lck, o);
        return current->next_lock;
    }

    // Sinon on passe au prochain lck
    if(current->len == 0 && current->type == F_WRLCK)
        max_wrlen = lck->l_start + 1; //On créera l'overlap pour la prochain étape
    else if(current->type == F_WRLCK && max_wrlen < current->starting_offset + current->len)
        max_wrlen = current->starting_offset + current->len;

    int next = rl_add_readlck(lock_table, current->next_lock, lck, o, max_wrlen);
    if(next <= -2) return next;
    current->next_lock = next;
    return pos;
}

// -2 -> no place
// -3 -> overlap
// max_wrlen indique le max de la borne gauche d'un lock write (pour les read seule les write nous embetent)
static int rl_add_writelck(rl_lock *lock_table, int pos, struct flock *lck, owner o, int max_len)
{

    //Overlap par le gauche !
    if(max_len > lck->l_start) return -3;

    rl_lock *current = lock_table + pos;

    if(lck->l_start <= current->starting_offset)
    {
        if(lck->l_len == 0) return -3;
        if(current->starting_offset < lck->l_start + lck->l_len) return -3;

        // -2 si pas de place
        return add_pos(lock_table, pos, lck, o);
    }    

    // Ajout à la fin !
    if(current->next_lock == -1)
    {
        //Si overlap
        if((current->len == 0 || current->len + current->starting_offset > lck->l_start))
            return -3;
        
        current->next_lock = add_pos(lock_table, -1, lck, o);
        return current->next_lock;
    }

    // Sinon on passe au prochain lck
    if(current->len == 0)
        max_len = lck->l_start + 1; //On créera l'overlap pour la prochain étape
    else if(max_len < current->starting_offset + current->len)
        max_len = current->starting_offset + current->len;

    int next = rl_add_writelck(lock_table, current->next_lock, lck, o, max_len);
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

static int rl_cut(rl_lock *lock_table, rl_lock *current, int cut_pos)
{
    int old_len = current->len;
    current->len = (cut_pos - current->starting_offset);

    int i = 0;
    for(i = 0; i < NB_LOCKS; i++)
        if(lock_table[i].next_lock != -2) break;
    if(i == NB_LOCKS) return -2;

    // Création du bloc
    lock_table[i].next_lock = current->next_lock;
    current->next_lock = i;
    lock_table[i].starting_offset = cut_pos;
    lock_table[i].len = current->starting_offset + old_len - cut_pos;
    lock_table[i].nb_owners = current->nb_owners;
    for(int j = 0; j < current->nb_owners; i++)
        lock_table[i].lock_owners[j] = current->lock_owners[j];
    
    return i;
}

static int rl_unlock(rl_lock *lock_table, int pos, struct flock *lck, owner o)
{
    //Si on est arrivé à la fin de la liste
    if(pos == -1 || pos == -2) return pos;
    rl_lock *current = lock_table + pos;
    
    // On doit couper le milieu du bloc [**|*******]
    if(current->starting_offset < lck->l_start && lck->l_start < current->starting_offset + current->len && is_in_lock(current, o))
    {
        //On doit add un lock avec tous les autres dedans plus court
        int r = rl_cut(lock_table, current, lck->l_start);
        if(r == -2) return -3;

        // On enlève pas de suite le owner, on le fera plus tard
        int next = rl_unlock(lock_table, current->next_lock, lck, o);
        if(next == -2) return -3;
        current->next_lock = next;
        return pos;
    }
    
    // Soit c'est bien aligné à gauche parce que l'utilisateur a demandé ça
    // Soit c'est une coupe faite par le bloc du dessus
    // Mais là y une autre coupe à faire [|******|***]
    if(lck->l_start + lck->l_len < current->starting_offset + current->len && is_in_lock(current, o))
    {
        // On coupe le bloc
        int r = rl_cut(lock_table, current, lck->l_start + lck->l_len);
        if(r == -2) return -3;

        // On enlève pas de suite le owner, on le fera plus tard
        // On rappelle sur la current pos, pour enlever le owner et peut le bloc en question
        return rl_unlock(lock_table, pos, lck, o);
    }    

    // Là on en enlève le owner et peut être le bloc si jamais [|*********|]
    if(lck->l_start <= current->starting_offset && current->starting_offset + current->len <= lck->l_start + lck->l_len && is_in_lock(current, o))
    {
        remove_owner_in_lock(lock_table, pos, o);

        int next = rl_unlock(lock_table, current->next_lock, lck, o);
        if(next == -2) return -3;
        current->next_lock = next;

        //On elève si besoin
        if(current->nb_owners == 0)
            return current->next_lock;
        return pos;
    }

    int next = rl_unlock(lock_table, current->next_lock, lck, o);
    if(next == -2) return -3;
    current->next_lock = next;
    return pos;

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
        int r = rl_unlock(lfd.f->lock_table, lfd.f->first, lck, o);
        pthread_mutex_unlock(&lfd.f->mutex_list);
        if(r == -2) return r;
        return 0;
    }

    int pos = rl_find(lfd.f->lock_table, lfd.f->first, lck);

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
        if(lck->l_type == F_WRLCK)
            pos = rl_add_writelck(lfd.f->lock_table, lfd.f->first, lck, o, 0);
        if(lck->l_type == F_RDLCK)
            pos = rl_add_readlck(lfd.f->lock_table, lfd.f->first, lck, o, 0);
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
        //! Promotion ne sont pas clair pour moi à demander au prof
        printf("Pas encore fait, dsl T_T\n");
        //Si il n est pas proprietaire dans le verrou
            // Si il est de meme type on l'ajoute
            // Sinon on refuse....
                    // Si c etait un read et qu on met un write pbm
                    // si c etait un write et qu on met un read pbm
        
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

static void rl_print_owner(owner o)
{
    printf("(d = %d, proc = %d) ", o.des, o.proc);
}

static void rl_print_lock(rl_lock *lck)
{
    printf("{start = %ld, end = %ld, type = %d ,", lck->starting_offset, lck->starting_offset + lck->len, lck->type);
    for(int i = 0; i < lck->nb_owners; i++)
        rl_print_owner(lck->lock_owners[i]);
    printf("\n");
}

static void rl_print_lock_table(rl_lock *lock_table, int pos)
{
    if(pos == -1) return;
    rl_print_lock(lock_table + pos);
}

void rl_print_open_file(rl_open_file *f)
{
    if(f->first == -2)
    {
        printf("VIDE\n");
        return;
    }
    rl_print_lock_table(f->lock_table, f->first);
}