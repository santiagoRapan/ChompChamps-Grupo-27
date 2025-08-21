#include "game_functions.h"
#include "structs.h"
#include <fcntl.h>
#include <sys/mman.h>

int create_shared_memmory(const char* name, size_t size) {
    int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0644);
    if(shm_fd == -1){
        perror("Error al crear memoria compartida");
        return -1;
    }
    if(ftruncate(shm_fd, size) == -1){
        perror("Error al configurar el tamaño de la memoria compartida");
        close(shm_fd);
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

void initialize_board(game_state_t* state, unsigned int seed) {
    srand(seed);
    for (int i = 0; i < state->width * state->height; i++) {
        state->board[i] = (rand() % 9) + 1;
    }
}
