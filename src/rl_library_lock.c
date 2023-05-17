#include "rl_library_lock.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include "deadlock.h"

#define PREFIX "f"

/** Variable globale statique qui contient tous nos fichiers ouverts. */
static rl_all_file all_file;


/** DEADLOCK */
static deadlock *dlck;
static int dlck_init = 0;

void initialiser_deadlock();
int get_owner_lock(rl_lock *lock_table, int pos, struct flock *lck, owner o, owner *pred_own, int nb);
void add_deadlock(owner o, struct flock lck, char *shm);
void remove_deadlock(owner o);
int verif_lock(owner o, struct flock *lck, char *shm, char *shm_opened);

/**
 * Initialise un mutex 
 * 
 * Paramètre : 
 * - pmutex : le mutex à initialiser 
 * 
 * Retourne : 
 * - 0 en cas de succès
 * - 1 en cas d'erreur
 */
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

/**
 * Initialise une condition 
 * 
 * Paramètre : 
 * - pmutex : la condition à initialiser 
 * 
 * Retourne : 
 * - 0 en cas de succès
 * - 1 en cas d'erreur
 */
int initialiser_cond(pthread_cond_t *pcond) {
    pthread_condattr_t condattr;
    if (pthread_condattr_init(&condattr) != 0){
        perror("condattr init");
        return 1;
    }
    if (pthread_condattr_setpshared( &condattr, PTHREAD_PROCESS_SHARED) != 0) {
        perror("condattr setpshared");
        return 1;
    }

    if (pthread_cond_init( pcond, &condattr ) != 0){
        perror("cond init");
        return 1;
    }

    return 0;
}

int rl_init_library()
{
    all_file.nb_files = 0;
    return 0;
}

static rl_open_file *open_shm(const char *path)
{
    if(!dlck_init) initialiser_deadlock();
    struct stat st;
    if(stat(path, &st) == -1) 
    {
        perror("stat");
        return NULL;
    }

    // On construit la chaine de caractères
    char name[256];
    memset(name, 0, 256);
    snprintf(name, 256,"/%s_%ld_%ld", PREFIX, st.st_dev, st.st_ino);

    int fd = shm_open(name, O_RDWR | O_EXCL, S_IRWXU | S_IRWXG);

    int first = 0;

    // Le shm n'existe pas
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

    // On lit la mémoire partagée
    rl_open_file *file = mmap(0, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(file == MAP_FAILED)
    {
        perror("mmap");
        return NULL;
    }

    all_file.fd_shm[all_file.nb_files] = fd;

    if(first) 
    {
        //Initialise le file !
        file->first = -2;
        for(int i = 0; i < NB_LOCKS; i++)
            file->lock_table[i].next_lock = -2;
        if(initialiser_mutex(&file->mutex_list) != 0)
            return NULL;
        if(initialiser_cond(&file->cond_list) != 0){
            return NULL;
        }

        file->nb_des = 0;

        memset(file->shm, 0, 256);
        strncpy(file->shm, name, 256);
    }

    pthread_mutex_lock(&file->mutex_list);
    file->nb_des++;
    pthread_mutex_unlock(&file->mutex_list);

    return file;
}

/**
 * Ouvre un fichier.
 * 
 * Parametres :
 * - path : le chemin vers le fichier
 * - oflag : mode d'ouverture
 * Si oflag contient O_CREAT, il faut ajouter en plus les permissions dans le paramètre après.
 * 
 * Retourne : un rl_descriptor du fichier.
 * En cas d'erreur retourne {-1, NULL}
*/
rl_descriptor rl_open(const char *path, int oflag, ...)
{
    rl_descriptor des = {.d = -1, .f = NULL};
    if(all_file.nb_files == NB_FILES) return des;
    int fd;

    if (oflag & O_CREAT) {
        va_list mode;
        va_start(mode, oflag);
        int m = va_arg(mode, int);
        fd = open(path, oflag, m);
        va_end(mode);
    }

    else fd = open(path, oflag);
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

int rl_close(rl_descriptor lfd)
{
    if(close(lfd.d) == -1) return -1;
    if(lfd.f->first != -2) {
        //On vérouille car on va modifier la liste (potentiellement)
        pthread_mutex_lock(&lfd.f->mutex_list);

        owner o = {.proc = getpid(), .des = lfd.d};
        lfd.f->first = rl_remove_owner(lfd.f->lock_table, lfd.f->first, o);


        // Cas si y a plus personne dans la liste (la fonction revoie -1 si c'est le cas)
        if(lfd.f->first == -1) lfd.f->first = -2;

        pthread_mutex_unlock(&lfd.f->mutex_list);

        // On réveille les processus attendant sur cond
        pthread_cond_signal(&lfd.f->cond_list);

        for(int i = 0; i < all_file.nb_files; i++) {
            if(all_file.tab_open_files[i] == lfd.f) {
                close(all_file.fd_shm[i]);
                for(int j = i; j < all_file.nb_files - 1; j++) {
                    all_file.tab_open_files[j] = all_file.tab_open_files[j + 1];
                }
                all_file.nb_files--;
                break;
            }
        }
    }

    pthread_mutex_lock(&lfd.f->mutex_list);
    lfd.f->nb_des--;
    if(lfd.f->nb_des == 0) {
        shm_unlink(lfd.f->shm);
    }
    pthread_mutex_unlock(&lfd.f->mutex_list);

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
    lock_table[i].nb_owners = 1;
    lock_table[i].starting_offset = lck->l_start;
    lock_table[i].type = lck->l_type;
    lock_table[i].next_lock = pos;
    return i;
}

static int is_the_only_one(rl_lock *lck, owner o)
{
    return lck->nb_owners == 1 && lck->lock_owners[0].des == o.des && lck->lock_owners[0].proc == o.proc;
}

// return -1 si personne
static int get_min_start_write(rl_lock *lock_table, int pos, owner o)
{
    rl_lock *current = lock_table + pos;
    if(current->next_lock == -1 && current->type == F_WRLCK && !is_the_only_one(current, o)) return current->starting_offset;
    if(current->type == F_WRLCK && !is_the_only_one(current, o)) return current->starting_offset;
    if(current->next_lock == -1) return -1;
    int next_min = get_min_start_write(lock_table, current->next_lock, o);
    return next_min;
}

// return -1 si personne
static int get_min_start(rl_lock *lock_table, int pos, owner o)
{
    rl_lock *current = lock_table + pos;
    if(current->next_lock == -1 && !is_the_only_one(current, o)) return current->starting_offset;
    if(!is_the_only_one(current, o)) return current->starting_offset;
    if(current->next_lock == -1) return -1;
    int next_min = get_min_start(lock_table, current->next_lock, o);
    return next_min;
}


static int get_max_len(rl_lock *lock_table, int pos, owner o, int l_pos)
{
    if(l_pos == pos) return -1;
    rl_lock *current = lock_table + pos;
    int max_len = get_max_len(lock_table, current->next_lock, o, l_pos);
    if(!is_the_only_one(current, o) && current->starting_offset + current->len > max_len)
        return current->starting_offset + current->len;
    return max_len;
}

// -2 -> no place
// -3 -> overlap
// max_wrlen indique le max de la borne gauche d'un lock write (pour les read seule les write nous embetent)
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
        if(current->type == F_WRLCK && (current->len == 0 || current->len + current->starting_offset > lck->l_start) && !is_the_only_one(current, o))
            return -3;
        
        int r = add_pos(lock_table, -1, lck, o);
        if(r == -2) return -2;
        current->next_lock = r;
        return pos;
    }

    // Sinon on passe au prochain lck
    if(current->len == 0 && current->type == F_WRLCK && !is_the_only_one(current, o))
        max_wrlen = lck->l_start + 1;
    else if(current->type == F_WRLCK && max_wrlen < current->starting_offset + current->len && !is_the_only_one(current, o))
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

        int min_rstart = get_min_start(lock_table, pos, o);
        if(min_rstart != -1 && lck->l_len == 0) return -3;
        if(min_rstart != -1 && min_rstart < lck->l_start + lck->l_len) return -3;

        // -2 si pas de place
        return add_pos(lock_table, pos, lck, o);
    }    

    // Ajout à la fin !
    if(current->next_lock == -1)
    {
        //Si overlap
        if((current->len == 0 || current->len + current->starting_offset > lck->l_start))
            return -3;
        
        int r = add_pos(lock_table, -1, lck, o);
        if(r == -2) return -2;
        current->next_lock = r;
        return pos;
    }

    // Sinon on passe au prochain lck
    if(current->len == 0 && !is_the_only_one(current, o))
        max_len = lck->l_start + 1; //On créera l'overlap pour la prochain étape
    else if(max_len < current->starting_offset + current->len && !is_the_only_one(current, o))
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

    printf("rl_cut at %d\n", cut_pos);
    int i = 0;
    for(i = 0; i < NB_LOCKS; i++)
        if(lock_table[i].next_lock == -2) break;
    if(i == NB_LOCKS) return -2;

    // Création du bloc
    lock_table[i].next_lock = current->next_lock;
    lock_table[i].type = current->type;
    current->next_lock = i;
    lock_table[i].starting_offset = cut_pos;
    lock_table[i].len = current->starting_offset + old_len - cut_pos;
    lock_table[i].nb_owners = current->nb_owners;
    
    for(int j = 0; j < current->nb_owners; j++)
        lock_table[i].lock_owners[j] = current->lock_owners[j];
    
    
    return i;
}

static int rl_unlock(rl_lock *lock_table, int pos, struct flock *lck, owner o, int first)
{
    if(pos == -1 || pos == -2) return pos;
    rl_lock *current = lock_table + pos;

    // On doit couper le milieu du bloc [**|*******]
    if(current->starting_offset < lck->l_start && lck->l_start < current->starting_offset + current->len && is_in_lock(current, o))
    {
        //On doit add un lock avec tous les autres dedans plus court
        int r = rl_cut(lock_table, current, lck->l_start);
        if(r == -2) return -3;

        // On enlève pas de suite le owner, on le fera plus tard
        int next = rl_unlock(lock_table, current->next_lock, lck, o, first);
        if(next == -2 || next == -3) return -3;
        current->next_lock = next;
        return pos;
    }
    
    // Soit c'est bien aligné à gauche parce que l'utilisateur a demandé ça
    // Soit c'est une coupe faite par le bloc du dessus
    // Mais là y une autre coupe à faire [|******|***]
    if(current->starting_offset < lck->l_start + lck->l_len && lck->l_start + lck->l_len < current->starting_offset + current->len && is_in_lock(current, o))
    {
        // On coupe le bloc
        int r = rl_cut(lock_table, current, lck->l_start + lck->l_len);
        if(r == -2) return -3;

        // On enlève pas de suite le owner, on le fera plus tard
        // On rappelle sur la current pos, pour enlever le owner et peut le bloc en question
        return rl_unlock(lock_table, pos, lck, o, first);
    }    

    // Là on en enlève le owner et peut être le bloc si jamais [|*********|]
    if(lck->l_start <= current->starting_offset && current->starting_offset + current->len <= lck->l_start + lck->l_len && is_in_lock(current, o))
    {
        remove_owner_in_lock(lock_table, pos, o);

        int next = rl_unlock(lock_table, current->next_lock, lck, o, first);
        if(next == -2 || next == -3) return -3;
        current->next_lock = next;

        //On elève si besoin
        if(current->nb_owners == 0){
            int rep = current->next_lock;
            current->next_lock = -2;
            return rep;
        }
        return pos;
    }

    int next = rl_unlock(lock_table, current->next_lock, lck, o, first);
    if(next == -2 || next == -3) return -3;
    current->next_lock = next;
    return pos;

}

static int rl_add_lock(rl_descriptor lfd, int cmd, struct flock *lck)
{
    //Les read peuvent s'overlapper
    //Les write - read / write - write peuvent pas
    owner o = {.proc = getpid(), .des = lfd.d};
    int pos = -2;
    if(lfd.f->first == -2)
    {
        pos = 0;
        rl_lock *lock_table = lfd.f->lock_table;
        lock_table[0].nb_owners = 1;
        lock_table[0].len = lck->l_len;
        lock_table[0].lock_owners[0] = o;
        lock_table[0].starting_offset = lck->l_start;
        lock_table[0].type = lck->l_type;
        lock_table[0].next_lock = -1;
    }
    else if(lck->l_type == F_WRLCK)
        pos = rl_add_writelck(lfd.f->lock_table, lfd.f->first, lck, o, 0);
    else if(lck->l_type == F_RDLCK)
        pos = rl_add_readlck(lfd.f->lock_table, lfd.f->first, lck, o, 0);
    
    // EAGAIN : Ressource temporairement non disponible
    if(pos == -3)
    {
        printf("Overlap\n");
        errno = EAGAIN;
        return -3;
    }
    if(pos == -2)
    {
        printf("No Place\n");
        errno = EAGAIN;
        return -2;
    }
    lfd.f->first = pos;
    return 0;
}

// -3 overlap !
static int rl_replace_lock(rl_descriptor lfd, struct flock *lck, int pos, owner o)
{
    rl_lock *current = lfd.f->lock_table + pos;

    //WRITE sur WRITE
    if(lck->l_type == F_WRLCK && current->type == F_WRLCK)
    {
        if(is_in_lock(current, o)) return 0;
        else return -3;
    }

    //READ sur WRITE
    if(lck->l_type == F_RDLCK && current->type == F_WRLCK)
    {
        if(is_the_only_one(current, o))
        {
            current->type = F_RDLCK;
            return 0;
        } else return -3;
    }


    if(current->type == F_RDLCK)
    {
        if(lck->l_type == F_RDLCK && is_in_lock(current, o)) return 0;
        if(lck->l_type == F_WRLCK && !is_the_only_one(current, o)) return -3;
        int min_wr = get_min_start_write(lfd.f->lock_table, pos, o);
        int max_wr = get_max_len(lfd.f->lock_table, lfd.f->first, o, pos);
        if(current->starting_offset < max_wr) return -3;
        if(min_wr != -1 && current->starting_offset + current->len > max_wr) return -3;
        if(lck->l_type == F_RDLCK)
        {
            if(current->nb_owners == NB_OWNERS) return -2;
            current->lock_owners[current->nb_owners] = o;
            current->nb_owners++;
        } else current->type = F_WRLCK;
    }
    return 0;

}

//TODO: Ajouter un merge des verrou

int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck)
{
    if(cmd != F_SETLK && cmd != F_SETLKW && cmd != F_GETLK) return -1;

    owner o = {.proc = getpid(), .des = lfd.d};

    // Si y a des processus propriétaire de lck non vivants, on les enlève.
    if (pthread_mutex_lock(&lfd.f->mutex_list) < 0) goto error_lock1;
    if(lfd.f->first != -2) {
        rl_lock *aux = lfd.f->lock_table + lfd.f->first;


        while (aux->next_lock != -1){
            // On balaye tous les propriétaires des verrous du fichier en mémoire partagée
            int limit = aux->nb_owners;
            for (int i = 0; i < limit; i ++){
                // On vérifie si le processus propriétaire existe, s'il n'existe pas on retire ses verrous
                if (kill(aux->lock_owners[i].proc, 0) == -1 && errno == ESRCH)
                    remove_owner_in_lock(aux, i, aux->lock_owners[i]);
            }
            aux = lfd.f->lock_table + aux->next_lock;
        }
        // On traite le dernier verrou
        int limit = aux->nb_owners;
        for (int i = 0; i < limit; i ++){
             if (kill(aux->lock_owners[i].proc, 0) == -1 && errno == ESRCH)
                remove_owner_in_lock(aux, i, aux->lock_owners[i]);
        }
    }
    if (pthread_mutex_unlock(&lfd.f->mutex_list) < 0) goto error_unlock1;


    if(lck->l_type == F_UNLCK)
    {
        if (pthread_mutex_lock(&lfd.f->mutex_list) < 0) goto error_lock2;
        int r = rl_unlock(lfd.f->lock_table, lfd.f->first, lck, o, lfd.f->first);
        if(r == -3) {
            pthread_mutex_unlock(&lfd.f->mutex_list);
            return r;
        }
        if(r == -1) lfd.f->first = -2;
        else lfd.f->first = r;
        pthread_mutex_unlock(&lfd.f->mutex_list);

        pthread_cond_signal(&lfd.f->cond_list);

        return 0;
    }

    // Cas du cmd == F_SETLK ou F_SETLKW
    
    if(cmd == F_SETLKW) pthread_mutex_lock(&dlck->mutex);
    if (pthread_mutex_lock(&lfd.f->mutex_list) < 0) goto error_lock2;
    while(1) {
        int pos = -2;
        if(lfd.f->first != -2 )
            pos = rl_find(lfd.f->lock_table, lfd.f->first, lck);

        // Si n'est pas dans les lck, on l'ajoute
        if(pos == -2)
        {   
            int r = rl_add_lock(lfd, cmd, lck);
            if(cmd == F_SETLKW) {
                if(r != -2 && r != -3) break;

                //printf("VERIF\n");

                // Je verifie
                int verif = verif_lock(o, lck, lfd.f->shm, lfd.f->shm);

               // printf("VERIF FIN\n");

                // Si pas bon je renvoie une erreur
                if(!verif) {
                    printf("DEADLOCK !\n");
                    pthread_mutex_unlock(&dlck->mutex);
                    pthread_mutex_unlock(&lfd.f->mutex_list);
                    //TODO: Changer le retour
                    return -3;
                } else {
                    // sinon je m ajoute à la liste 
                    add_deadlock(o, *lck, lfd.f->shm);
                }
                // j enleve le lock
                pthread_mutex_unlock(&dlck->mutex);

                pthread_cond_wait(&lfd.f->cond_list, &lfd.f->mutex_list);
                //J enleve le lock mutex list, je prend le deadlock, et je reprend le lock
                pthread_mutex_unlock(&lfd.f->mutex_list);
                pthread_mutex_lock(&dlck->mutex);
                pthread_mutex_lock(&lfd.f->mutex_list);
                //je m enleve de la liste
                remove_deadlock(o);
            } else {
                pthread_mutex_unlock(&lfd.f->mutex_list);
                return r;
            }

        } 

         else 
        {
            int r = rl_replace_lock(lfd, lck, pos, o);
            if(cmd == F_SETLKW) {
                if(r != -2 && r != -3) break;

                //printf("VERIF\n");

                // Je verifie
                int verif = verif_lock(o, lck, lfd.f->shm, lfd.f->shm);

                //printf("VERIF FIN\n");

                // Si pas bon je renvoie une erreur
                if(!verif) {
                    printf("DEADLOCK !\n");
                    pthread_mutex_unlock(&dlck->mutex);
                    pthread_mutex_unlock(&lfd.f->mutex_list);
                    //TODO: Changer le retour
                    return -3;
                } else {
                    // sinon je m ajoute à la liste 
                    //printf("AJOUT DANS DLCK\n");
                    add_deadlock(o, *lck, lfd.f->shm);
                }
                // j enleve le lock
                pthread_mutex_unlock(&dlck->mutex);

                pthread_cond_wait(&lfd.f->cond_list, &lfd.f->mutex_list);
                //J enleve le lock mutex list, je prend le deadlock, et je reprend le lock
                pthread_mutex_unlock(&lfd.f->mutex_list);
                pthread_mutex_lock(&dlck->mutex);
                pthread_mutex_lock(&lfd.f->mutex_list);
                //je m enleve de la liste
                remove_deadlock(o);
                //printf("RM DANS DLCK\n");
            } else {
                pthread_mutex_unlock(&lfd.f->mutex_list);
                return r;
            }
        }


    }
    pthread_mutex_unlock(&dlck->mutex);
    pthread_mutex_unlock(&lfd.f->mutex_list);

    return 0;

error_lock1 :
    perror("pthread_mutex_lock lors du nettoyage de rl_fcntl");
    return -1;

error_unlock1 :
    perror("pthread_mutex_unlock lors du nettoyage de rl_fcntl");
    return -1;

error_lock2 :
    perror("pthread_mutex_lock de rl_fcntl");
    return -1;

error_unlock2 :
    perror("pthread_mutex_unlock de rl_fcntl");
    return -1;
}

/**
 * Duplique un descripteur sur le plus petit descipteur non utilisé.
 * 
 * Parametres :
 * - lfd : Le rl_descriptor qu'on veut dupliquer.
 * 
 * Retourne : un rl_descriptor. 
 * En cas d'erreur il faut regarder la valeur du descripteur d.
 * -1 : dup ou dup2 a échoué.
 * -2 : Plus de place dans le tableau des propriétaires.
 * -3 : verouillage a échoué.
 * -4 : dévérouillage a échoué.
*/
rl_descriptor rl_dup(rl_descriptor lfd)
{
    // On commence par dupliquer le descripteur
    int newd = dup(lfd.d);
    if (newd < 0) goto error_dup;

    // On stocke le pid pour éviter trop d'appels à getpid
    int pid = getpid();

    rl_descriptor new_rl_descriptor = {.d = newd, .f = lfd.f};
    if (pthread_mutex_lock(&lfd.f->mutex_list) < 0) goto error_lock;
    if(lfd.f->first == -2) {
        pthread_mutex_unlock(&lfd.f->mutex_list);
        return new_rl_descriptor;
    }

    // On duplique toutes les occurrences de lfd_owner comme propriétaire de verrou
    // On le fait de manière itérative
    rl_lock *aux = lfd.f->lock_table + lfd.f->first;


    while (aux->next_lock != -1){
        // On balaye tous les propriétaires des verrous du fichier en mémoire partagée
        int limit = aux->nb_owners;
        for (int i = 0; i < limit; i ++){
            // si on a un propriétaire {pid, lfd.d} on ajoute {pid, newd} aux verrous
            if (aux->lock_owners[i].des == lfd.d && aux->lock_owners[i].proc == pid){
                owner lfd_owner = { .proc = pid, .des = newd };
                if (aux->nb_owners == NB_OWNERS) goto error_nb_owners;

                aux->lock_owners[aux->nb_owners] = lfd_owner;
                aux->nb_owners ++;
            }
        }
        aux = lfd.f->lock_table + aux->next_lock;
    }
    // On traite le dernier verrou
    int limit = aux->nb_owners;
    for (int i = 0; i < limit; i ++){
        if (aux->lock_owners[i].des == lfd.d && aux->lock_owners[i].proc == pid){
            owner lfd_owner = { .des = newd, .proc = pid };
            if (aux->nb_owners == NB_OWNERS) goto error_nb_owners;

            aux->lock_owners[aux->nb_owners] = lfd_owner;
            aux->nb_owners ++;
        }
    }

    if (pthread_mutex_unlock(&lfd.f->mutex_list) < 0) goto error_unlock;

    // On retourne le nouveay rl_descriptor
    return new_rl_descriptor;

// Cas d'erreurs
error_dup:
    rl_descriptor error1 = {.d = -1, .f = NULL};
    return error1;

error_nb_owners:
    if (pthread_mutex_unlock(&lfd.f->mutex_list) < 0) goto error_unlock;
    rl_descriptor error2 = {.d = -2, .f = NULL};
    return error2;

error_lock :
    rl_descriptor error3 = {.d = -3, .f = NULL};
    return error3;

error_unlock :
    rl_descriptor error4 = {.d = -4, .f = NULL};
    return error4;
}

/**
 * Duplique un descripteur sur un descripteur donné. Retire l'ancien descripteur
 * 
 * Paramètre:
 * - lfd : Le rl_descriptor qu'on veut dupliquer.
 * - newd : Le nouveau descripteur de fichier.
 * 
 * Retourne : un rl_descriptor. En cas d'erreur il faut regarder la valeur du descripteur d.
 * -1 : dup ou dup2 a échoué.
 * -2 : Plus de place dans le tableau des propriétaires.
 * -3 : Le verouillage a échoué.
 * -4 : Le dévérouillage a échoué.
*/
rl_descriptor rl_dup2(rl_descriptor lfd, int newd){
    // On commence par dupliquer le descripteur
    if (dup2(lfd.d, newd) < 0) goto error_dup;

    // On stocke le pid pour éviter trop d'appels à getpid
    int pid = getpid();

    rl_descriptor new_rl_descriptor = {.d = newd, .f = lfd.f};

    if (pthread_mutex_lock(&lfd.f->mutex_list) < 0) goto error_lock;
    if(lfd.f->first == -2) {
        pthread_mutex_unlock(&lfd.f->mutex_list);
        return new_rl_descriptor;
    }
    // On duplique toutes les occurrences de lfd_owner comme propriétaire de verrou
    // On le fait de manière itérative
    rl_lock *aux = lfd.f->lock_table + lfd.f->first;

    while (aux->next_lock != -1){
        // On balaye tous les propriétaires des verrous du fichier en mémoire partagée
        int limit = aux->nb_owners;
        for (int i = 0; i < limit; i ++){
            // si on a un propriétaire {pid, lfd.d} on ajoute {pid, newd} aux verrous
            if (aux->lock_owners[i].des == lfd.d && aux->lock_owners[i].proc == pid)
                aux->lock_owners[i].des = newd;
        }
        aux = lfd.f->lock_table + aux->next_lock;
    }
    // On traite le dernier verrou
    int limit = aux->nb_owners;
    for (int i = 0; i < limit; i ++){
        if (aux->lock_owners[i].des == lfd.d && aux->lock_owners[i].proc == pid)
            aux->lock_owners[i].des = newd;
    }

    if (pthread_mutex_unlock(&lfd.f->mutex_list) < 0) goto error_unlock;

    // On retourne le nouveay rl_descriptor
    return new_rl_descriptor;

// Cas d'erreurs
error_dup:
    rl_descriptor error1 = {.d = -1, .f = NULL};
    return error1;

error_lock :
    rl_descriptor error3 = {.d = -3, .f = NULL};
    return error3;

error_unlock :
    rl_descriptor error4 = {.d = -4, .f = NULL};
    return error4;
}

/**
 * Meme comportement que fork.
 * 
 * Retourne : un pid_t
 * En cas d'erreur ce pid_t vaut :
 * -1 : fork a échoué
 * -2 : Plus de place dans le tableau des propriétaires.
 * -3 : Le verouillage a échoué.
 * -4 : Le dévérouillage a échoué.
*/
pid_t rl_fork(){
    int f = fork();
    if (f != 0) return f; // Parent ou erreur
    // Dans l'enfant
    // On stocke ppid et pid
    pid_t ppid = getppid();
    pid_t pid = getpid();

    // On cherche dans les fichiers les verrous que le parent possède
    for(int i = 0; i < all_file.nb_files; i++){
        pthread_mutex_t *l = &all_file.tab_open_files[i]->mutex_list;
        if (pthread_mutex_lock(l) < 0) return -3;

        if(all_file.tab_open_files[i]->first == -2) 
        {
            if (pthread_mutex_unlock(l) < 0) return -4;
            continue;
        }

        // aux est un verrou
        rl_lock *aux = (all_file.tab_open_files[i]->lock_table) + (all_file.tab_open_files[i]->first);
        
        while(aux->next_lock != -1){
            // On balaye tous les propriétaires du verrou
            int limit = aux->nb_owners;
            for (int i = 0; i < limit; i ++){
                // si on a un propriétaire {ppid, d} on ajoute {pid, d} aux verrous
                if (aux->lock_owners[i].proc == ppid){
                    if (aux->nb_owners == NB_OWNERS) return -2;
                    owner lfd_owner = { .proc = pid, .des = aux->lock_owners[i].des };
                    
                    aux->lock_owners[aux->nb_owners] = lfd_owner;
                    aux->nb_owners ++;
                }
            }
            aux = all_file.tab_open_files[i]->lock_table + aux->next_lock;
        }

        // On traite le dernier verrou
        int limit = aux->nb_owners;
        for (int i = 0; i < limit; i ++){
            if (aux->lock_owners[i].proc == ppid){
                if (aux->nb_owners == NB_OWNERS) return -2;
                owner lfd_owner = { .proc = pid, .des = aux->lock_owners[i].des };
                
                aux->lock_owners[aux->nb_owners] = lfd_owner;
                aux->nb_owners ++;
            }
        }
        if (pthread_mutex_unlock(l) < 0) return -4;
    }

    return 0;
}

static void rl_print_owner(owner o)
{
    printf("(d = %d, proc = %d) ", o.des, o.proc);
}

static void rl_print_lock(rl_lock *lck)
{
    printf("{start = %ld, end = %ld, type = %d , next = %d", lck->starting_offset, lck->starting_offset + lck->len, lck->type, lck->next_lock);
    for(int i = 0; i < lck->nb_owners; i++)
        rl_print_owner(lck->lock_owners[i]);
    printf("}\n");
}

static void rl_print_lock_table(rl_lock *lock_table, int pos)
{
    if(pos == -1) return;
    rl_print_lock(lock_table + pos);
    rl_print_lock_table(lock_table, (lock_table + pos)->next_lock);
}

void rl_print_open_file(rl_open_file *f)
{
    if(f->first == -2)
    {
        printf("VIDE\n");
        return;
    }
    rl_print_lock_tab(f->lock_table, f->first);
}

void rl_print_lock_tab(rl_lock *lock, int first)
{
    if(first == -2)
    {
        printf("VIDE\n");
        return;
    }
    rl_print_lock_table(lock, first);
}


// DEADLOCKS

void initialiser_deadlock()
{
    int fd = shm_open("/f_deadlock", O_RDWR | O_EXCL, S_IRWXU | S_IRWXG);
    int first = 0;
    if(fd == -1 && errno == ENOENT) {
        first = 1;
        fd = shm_open("/f_deadlock", O_RDWR | O_CREAT, S_IRWXU | S_IRWXG);
        if(fd == -1) {
            perror("open deadlock !");
            exit(1);
        }
        ftruncate(fd, sizeof(deadlock));
    }

    dlck = mmap(0, sizeof(deadlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(dlck == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    if(first) 
    {
        dlck->nb = 0;
        initialiser_mutex(&dlck->mutex);
    }

    dlck_init = 1;
}


int get_owner_lock(rl_lock *lock_table, int pos, struct flock *lck, owner o, owner *pred_own, int nb)
{
    if(pos == -2 || pos == -1) return nb;
    rl_lock *current = lock_table + pos;

    //Chevauche
    if((lck->l_start <= current->starting_offset && lck->l_start + lck->l_len > current->starting_offset)
    || (current->starting_offset <= lck->l_start && current->starting_offset + current->len > lck->l_start)
    || (lck->l_len == 0 && lck->l_start <= current->starting_offset)
    || (current->len == 0 && current->starting_offset <= lck->l_start))
    {
        //Si l'un des 2 est write, le chevauchement est prohibé
        if(lck->l_type == F_WRLCK || current->type == F_WRLCK) {
            //On parcourt
            for(int i = 0; i < current->nb_owners; i++) {
                owner oc = current->lock_owners[i];
                //Si c'est un owner différent de nous, on l'ajoute que si il est pas déjà ajouté
                if(oc.des != o.des || oc.proc != o.proc) {
                    int found = 0;
                    for(int j = 0; j < nb; j++) {
                        owner op = pred_own[j];
                        if(op.des == oc.des && op.proc == oc.proc) {
                            found = 1;
                            break;
                        }
                    }
                    if(!found) {
                        pred_own[nb].des = oc.des;
                        pred_own[nb].proc = oc.proc;
                        nb++;
                    }
                }
            }
        }
    }

    //Appelle récursif
    return get_owner_lock(lock_table, current->next_lock, lck, o, pred_own, nb);
}

void add_deadlock(owner o, struct flock lck, char *shm)
{
    //printf("nb = %d ADD : %s\n", dlck->nb, shm);
    dlck->o[dlck->nb] = o;
    dlck->lck[dlck->nb] = lck;
    memset(dlck->shm[dlck->nb], 0, MAX_LEN_SHM);
    strncpy(dlck->shm[dlck->nb], shm, MAX_LEN_SHM);
    //printf("FIN ADD : %s\n", dlck->shm[dlck->nb]);
    dlck->nb++;
}

void remove_deadlock(owner o)
{
    for(int i = 0; i < dlck->nb; i++) {
        if(o.proc == dlck->o[i].proc && o.des == dlck->o[i].des) {
            for(int j = i; j < dlck->nb - 1; j++) {
                dlck->o[j] = dlck->o[j + 1];
                dlck->lck[j] = dlck->lck[j + 1];
                strncpy(dlck->shm[j], dlck->shm[j + 1], MAX_LEN_SHM);
            }
            dlck->nb--;
            break;
        }
    }
}

int is_in_deadlock(owner o)
{
    for(int i = 0; i < dlck->nb; i++) {
        //printf(" - %d %d\n", dlck->o[i].proc, dlck->o[i].des);
        if(o.proc == dlck->o[i].proc) return i;
    }
    return -1;
}

// Fonction permettant de dire si oui ou non y aura un deadlock
// On regarde les owner qui gene le owner o qui veut le lock lck dans le fichier shm
// Si on trouve un owner avec un pid == getpid(), on a un deadlock
// Si on trouve un owner qui dort (i.e. dans le tableau de deadlock dans le shm)
// Alors il faut regarder si lui peut etre bloqué par nous
//TODO: Changer la fonction d'ouverture car provoque trop de fd non fermé

rl_descriptor open_shm_dead(char *shm)
{
    int fd = shm_open(shm, O_RDWR, S_IRWXU | S_IRWXG);
    if(fd == -1) {
        perror("open shm dead");
        exit(1);
    }
    rl_open_file *file = mmap(0, sizeof(rl_open_file), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if(file == MAP_FAILED) {
        perror("mmap open shm dead");
        exit(1);
    }
    rl_descriptor des = {fd, file};
    return des;
}


int verif_lock(owner o, struct flock *lck, char *shm, char *shm_opened)
{
    owner pred_own[256];
    memset(pred_own, 0, sizeof(owner) * 256);

    // printf("shm = %s\n", shm);
    // printf("shm_opened = %s\n",shm_opened);
    // printf("des : %d proc : %d\n", o.des, o.proc);

    rl_descriptor des = open_shm_dead(shm);

    rl_open_file *file = des.f;
    if(strcmp(shm_opened, shm)) {
        pthread_mutex_lock(&file->mutex_list);
    }
    //printf("A : ");
    //rl_print_open_file(file);

    //printf("DEBUT\n");
    int nb = get_owner_lock(file->lock_table, file->first, lck, o, pred_own, 0);
    //printf("FIN\n");
    if(strcmp(shm_opened, shm)) {
        pthread_mutex_unlock(&file->mutex_list);
        close(des.d);
    }

    // for(int i = 0; i < nb; i++) {
    //     printf("(%d, %d) ", pred_own[i].des, pred_own[i].proc);
    // }
    // printf("\n");

    pid_t p = getpid();
    for(int i = 0; i < nb; i++) {
        if(pred_own[i].proc == p) return 0;
        int index = is_in_deadlock(pred_own[i]);
        //printf("index = %d\n", index);
        if(index != -1) {
            int r = verif_lock(dlck->o[index], dlck->lck + index, dlck->shm[index], shm_opened);
            if(r == 0) return 0;
        }
    }
    return 1;
}