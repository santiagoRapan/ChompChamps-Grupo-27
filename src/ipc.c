#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include "structs.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>

int create_shared_memory(const char* name, size_t size) {
    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0644);
    if (shm_fd == -1){
        perror("Error al crear memoria compartida");
        return -1;
    }
    if (ftruncate(shm_fd, size) == -1){
        perror("Error al configurar el tamaño de la memoria compartida");
        close(shm_fd);
        return -1;
    }
    return shm_fd;
}

int connect_to_shared_memory(const char* name, bool read_only) {
    int flags = read_only ? O_RDONLY : O_RDWR;
    int shm_fd = shm_open(name, flags, 0);
    if (shm_fd == -1) {
        perror("shm_open connect");
        return -1;
    }
    return shm_fd;
}

int destroy_shared_memory(const char* name) {
    if(shm_unlink(name) == -1){
        perror("Error al destruir memoria compartida");
        return -1;
    }
    return 0;
}

void *attach_shared_memory(int shm_fd, size_t size, bool read_only) {
    int prot = read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* ptr = mmap(0, size, prot, MAP_SHARED,shm_fd, 0);
    if(ptr == MAP_FAILED){
        perror("Error al mapear memoria compartida");
        return NULL;
    }
    return ptr;
}

void detach_shared_memory(void* addr, size_t size) {
    if (munmap(addr, size) == -1) {
        perror("munmap");
    }
}

void cleanup_shared_memory(game_state_t* gamestate, game_sync_t* gamesync) {
    if(gamestate){
        size_t state_size = sizeof(game_state_t) + (gamestate->width * gamestate->height * sizeof(int));
        detach_shared_memory(gamestate, state_size);
    }
    if(gamesync){
        detach_shared_memory(gamesync, sizeof(game_sync_t));
    }

}

void clear_shm(const char* name){
    if (shm_unlink(name) == -1) {
        perror("shm_unlink");
    }
}


game_state_t* setup_game_state(int width, int height){
    int fd = connect_to_shared_memory(GAME_STATE_SHM, true);
    size_t state_size = sizeof(game_state_t) + (width * height * sizeof(int));
    game_state_t* game_state = (game_state_t*)attach_shared_memory(fd, state_size, true);
    if(!game_state){
        close(fd);
        return NULL;
    }
    close(fd);
    return game_state;
}

game_sync_t* setup_game_sync(){
    int fd = connect_to_shared_memory(GAME_SYNC_SHM, false);
    if (fd == -1) {
        return NULL;
    }
    game_sync_t* game_sync = (game_sync_t*)attach_shared_memory(fd, sizeof(game_sync_t), false);
    close(fd);
    if (!game_sync) {
        return NULL;
    }
    return game_sync;        
}

void initialize_semaphores(game_sync_t* sync, int player_count){
    sem_init(&sync->view_notify, 1, 0);
    sem_init(&sync->view_done, 1, 0);
    sem_init(&sync->writer_mutex, 1, 1);
    sem_init(&sync->state_mutex, 1, 1);
    sem_init(&sync->reader_count_mutex, 1, 1);
    sync->readers_count = 0;
    
    for (int i = 0; i < player_count; i++) {
        sem_init(&sync->player_turn[i], 1, 1);
    }
}

void cleanup_semaphores(game_sync_t* sync, int player_count) {
    sem_destroy(&sync->view_notify);
    sem_destroy(&sync->view_done);
    sem_destroy(&sync->writer_mutex);
    sem_destroy(&sync->state_mutex);
    sem_destroy(&sync->reader_count_mutex);
    
    for (int i = 0; i < player_count; i++) {
        sem_destroy(&sync->player_turn[i]);
    }
}

int is_executable_file(const char *path) {
    if (!path) return 0;
    if (access(path, F_OK | X_OK) != 0) return 0; // exists & executable
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode); // regular file
}