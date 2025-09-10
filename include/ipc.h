#ifndef IPC_H
#define IPC_H
#include "structs.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

// memoria compartida
int create_shared_memory(const char* name, size_t size);
void *attach_shared_memory(int shm_fd, size_t size, bool read_only);
void detach_shared_memory(void* addr, size_t size);
void cleanup_shared_memory(game_state_t* gamestate, game_sync_t* gamesync);
void clear_shm(const char* name);
int connect_to_shared_memory(const char* name, bool read_only);
game_state_t* setup_game_state(int width, int height);
game_sync_t* setup_game_sync();

// semaforos
void initialize_semaphores(game_sync_t* sync, int player_count);
void cleanup_semaphores(game_sync_t* sync, int player_count);

// funciones auxiliares
int is_executable_file(const char *path);

#endif