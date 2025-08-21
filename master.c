#include "Structs.h"
#include <stdio.h>
#include <errno.h>
#define DEFAULT_WIDTH 10
#define DEFAULT_HEIGHT 10
#define DEFAULT_DELAY 200 //MILISEGUNDOS
#define DEFAULT_TIMEOUT 100

typedef struct {
    int width;
    int height;
    int delay;
    int timeout;
    unsigned int seed;
    char* view_path;
    char* player_paths[MAX_PLAYERS];
    int player_count;
} master_config;

static game_state_t* game_state = NULL;
static game_sync_t* game_sync = NULL;
static player_t players[MAX_PLAYERS];
static pid_t view_pid = -1;
static int state_shm_fd = -1;
static int sync_shm_fd = -1;

void parser(master_config* config, int argc, char *argv[]){
    // Valores por defecto
    config->width = 10;
    config->height = 10;
    config->delay = 200;
    config->timeout = 10;
    config->seed = time(NULL);
    config->view_path = NULL;
    config->player_count = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            config->width = atoi(argv[++i]);
            if (config->width < 10) config->width = 10;
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            config->height = atoi(argv[++i]);
            if (config->height < 10) config->height = 10;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            config->delay = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            config->timeout = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            config->seed = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            config->view_path = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0) {

            while (i + 1 < argc && argv[i + 1][0] != '-' && config->player_count < MAX_PLAYERS) {
                config->player_paths[config->player_count++] = argv[++i];
            }
        }
    }
    
    if (config->player_count == 0) {
        fprintf(stderr, "Error: Se requiere al menos un jugador (-p)\n");
        exit(1);
    }
}

int setup_shared_memory(master_config* config) {
    size_t state_size = sizeof(game_state_t) + (config->width * config->height * sizeof(int));

    state_shm_fd = create_shared_memory(GAME_STATE_SHM, state_size);
    if(state_shm_fd == -1) return -1;

    game_state = (game_state_t*)attach_shared_memory(state_shm_fd, state_size, false);
    if(game_state == NULL) return -1;

    sync_shm_fd = create_shared_memory(GAME_SYNC_SHM, sizeof(game_sync_t));
    if (sync_shm_fd == -1) return -1;

    game_sync = (game_sync*)attach_shared_memory(sync_shm_fd, sizeof(game_sync_t), false);
    if(game_sync == NULL) return -1;
    
    game_state->width = config->width;
    game_state->height = config->height;
    game_state->players_count = config->player_count;
    game_state->is_game_over = false;

    for (int i = 0; i < config->player_count; i++) {
        snprintf(game_state->players[i].name, MAX_NAME_LENGTH, "Player%d", i);
        game_state->players[i].score = 0;
        game_state->players[i].invalid_moves = 0;
        game_state->players[i].valid_moves = 0;
        game_state->players[i].blocked = false;
        game_state->players[i].pid = 0;
    }


   return 0;
}


int main(int argc, char *argv[]){
    master_config config;
    parser(&config, argc, argv);

    
}

