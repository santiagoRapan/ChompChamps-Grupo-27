#define _POSIX_C_SOURCE 200809L
#include "game_functions.h"
#include "structs.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>


const int MOVE_DELTAS[8][2] = {
    {0, -1},  // UP
    {1, -1},  // UP_RIGHT
    {1, 0},   // RIGHT
    {1, 1},   // DOWN_RIGHT
    {0, 1},   // DOWN
    {-1, 1},  // DOWN_LEFT
    {-1, 0},  // LEFT
    {-1, -1}  // UP_LEFT
};

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

void initialize_board(game_state_t* state, unsigned int seed) {
    srand(seed);
    for (int i = 0; i < state->width * state->height; i++) {
        state->board[i] = (rand() % 9) + 1;
    }
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

bool is_valid_position(game_state_t* state, int x, int y) {
    return x >= 0 && x < state->width && y >= 0 && y < state->height;
}

bool is_cell_free(game_state_t* state, int x, int y) {
    if(!is_valid_position(state, x, y)){
        return false;
    }
    int cell_value = state->board[y * state->width + x];
    return cell_value > 0; // Valores positivos = celdas libres
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

void apply_move(game_state_t* game_state,int  player_id, unsigned char move) {//TERMINAR
    int current_x = game_state->players[player_id].x;
    int current_y = game_state->players[player_id].y;
    int new_x = current_x + MOVE_DELTAS[move][0];
    int new_y = current_y + MOVE_DELTAS[move][1];

    int reward = get_cell_value(game_state, new_x, new_y);

    game_state->players[player_id].x = new_x;
    game_state->players[player_id].y = new_y;

    game_state->players[player_id].score += reward;

    set_cell_owner(game_state, new_x, new_y, player_id);
    game_state->players[player_id].valid_moves++;
}

int is_valid_move(game_state_t* state, int player_id, unsigned char move) {
    if (move > 7) {
        return false;
    }
    
    if (player_id < 0 || (unsigned int)player_id >= state->player_count) {
        return false;
    }
    
    if (state->players[player_id].blocked) {
        return false;
    }
    
    int current_x = state->players[player_id].x;
    int current_y = state->players[player_id].y;
    
    int new_x = current_x + MOVE_DELTAS[move][0];
    int new_y = current_y + MOVE_DELTAS[move][1];
    
    return is_cell_free(state, new_x, new_y);
 }
