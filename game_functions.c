#include "game_functions.h"
#include "structs.h"
#include <fcntl.h>
#include <sys/mman.h>

int create_shared_memory(const char* name, size_t size) {
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

bool is_valid_position(game_state_t* state, int x, int y) {
    return x >= 0 && x < state->width && y >= 0 && y < state->height;
}

int get_cell_value(game_state_t* state, int x, int y) {
    if (!is_valid_position(state, x, y)) {
        return -1;
    }
    return state->board[y * state->width + x];
}

void set_cell_owner(game_state_t* state, int x, int y, int player_id) {
    if (is_valid_position(state, x, y)) {
        state->board[y * state->width + x] = -(player_id + 1);
    }
}

void place_players_on_board(game_state_t* state){
    int positions[][2] = {
        {1, 1},                                    // Jugador 1
        {state->width - 2, 1},                     // Jugador 2
        {1, state->height - 2},                    // Jugador 3
        {state->width - 2, state->height - 2},     // Jugador 4
        {state->width / 2, 1},                     // Jugador 5
        {1, state->height / 2},                    // Jugador 6
        {state->width - 2, state->height / 2},     // Jugador 7
        {state->width / 2, state->height - 2},     // Jugador 8
        {state->width / 2, state->height / 2}      // Jugador 9
    };
    
    for (unsigned int i = 0; i < state->player_count; i++) {
        state->players[i].x = positions[i][0];
        state->players[i].y = positions[i][1];
        
        // Marcar la celda inicial como ocupada (sin recompensa)
        set_cell_owner(state, positions[i][0], positions[i][1], (int)i);
    }
}
