#include "structs.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "game_functions.h"
#include <string.h>
#include <unistd.h>
#include <signal.h>
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
} master_config_t;

typedef struct {
    pid_t pid;
    int pipe_fd;
    bool active;
} player_process_t;

static game_state_t* game_state = NULL;
static game_sync_t* game_sync = NULL;
static player_process_t players[MAX_PLAYERS];
static pid_t view_pid = -1;
static int state_shm_fd = -1;
static int sync_shm_fd = -1;

// Helpers para el parseo de parametros 
static long parse_long_or_default(const char *s, long fallback) {
    if (s == NULL) return fallback;
    char *endptr = NULL;
    errno = 0;
    long val = strtol(s, &endptr, 10);
    if (errno != 0 || endptr == s || *endptr != '\0') {
        return fallback;
    }
    return val;
}

static unsigned int parse_uint_or_default(const char *s, unsigned int fallback) {
    if (s == NULL) return fallback;
    char *endptr = NULL;
    errno = 0;
    unsigned long val = strtoul(s, &endptr, 10);
    if (errno != 0 || endptr == s || *endptr != '\0' || val > UINT_MAX) {
        return fallback;
    }
    return (unsigned int)val;
}

void parser(master_config_t* config, int argc, char *argv[]){
    // Valores por defecto
    config->width = DEFAULT_WIDTH;
    config->height = DEFAULT_HEIGHT;
    config->delay = DEFAULT_DELAY;
    config->timeout = DEFAULT_TIMEOUT;
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

    //! Misma funcionalidad que lo de arriba pero con parseo robusto (Falta testear)
    /* 
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            long w = parse_long_or_default(argv[i+1], config->width);
            if (w < 10) w = 10;
            config->width = (int)w;
            i++;
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            long h = parse_long_or_default(argv[i+1], config->height);
            if (h < 10) h = 10;
            config->height = (int)h;
            i++;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            long d = parse_long_or_default(argv[i+1], config->delay);
            if (d < 0) d = config->delay;
            config->delay = (int)d;
            i++;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            long t = parse_long_or_default(argv[i+1], config->timeout);
            if (t < 0) t = config->timeout;
            config->timeout = (int)t;
            i++;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            config->seed = parse_uint_or_default(argv[i+1], config->seed);
            i++;
        } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            config->view_path = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0) {

            while (i + 1 < argc && argv[i + 1][0] != '-' && config->player_count < MAX_PLAYERS) {
                config->player_paths[config->player_count++] = argv[++i];
            }
        } else {
            // Unknown option or missing argument: skip
        }
    }
    */
    if (config->player_count == 0) {
        fprintf(stderr, "Error: Se requiere al menos un jugador (-p)\n");
        exit(1);
    }
}

int setup_shared_memory(master_config_t* config) {
    size_t state_size = sizeof(game_state_t) + (config->width * config->height * sizeof(int));

    state_shm_fd = create_shared_memory(GAME_STATE_SHM, state_size);
    if(state_shm_fd == -1) return -1;

    game_state = (game_state_t*)attach_shared_memory(state_shm_fd, state_size, false);
    if(game_state == NULL) return -1;

    sync_shm_fd = create_shared_memory(GAME_SYNC_SHM, sizeof(game_sync_t));
    if (sync_shm_fd == -1) return -1;

    game_sync = (game_sync_t*) attach_shared_memory(sync_shm_fd, sizeof(game_sync_t), false);
    if(game_sync == NULL) return -1;
    
    game_state->width = config->width;
    game_state->height = config->height;
    game_state->player_count = config->player_count;
    game_state->is_game_over = false;

    for (int i = 0; i < config->player_count; i++) {
        snprintf(game_state->players[i].name, MAX_NAME_LENGTH, "Player%d", i);
        game_state->players[i].score = 0;
        game_state->players[i].invalid_moves = 0;
        game_state->players[i].valid_moves = 0;
        game_state->players[i].blocked = false;
        game_state->players[i].pid = 0;
    }
    initialize_board(game_state, config->seed);

   return 0;
}

void clear_resources(){
    cleanup_shared_memory(game_state, game_sync);
    if(game_sync){
        cleanup_semaphores(game_sync, game_state->player_count);
    }
    if(state_shm_fd != -1){ 
        close(state_shm_fd);
        cleanup_shared_memory(GAME_STATE_SHM);
    }
    if(sync_shm_fd != -1){
        close(sync_shm_fd);
        cleanup_shared_memory(GAME_SYNC_SHM);
    }
    for(int i = 0; i < game_state->player_count; i++){
        if(players[i].pipe_fd != -1){
            close(players[i].pipe_fd);
        }
    }
}

pid_t create_player_process(const char* player_path, int player_id, master_config_t* config){
    int pipefd[2];
    if(pipe(pipefd) == -1){
        perror("Error al crear pipe");
        return -1;
    }

    pid_t pid = fork();
    if(pid == -1){
        perror("Error al crear proceso");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if(pid == 0){
        close(pipefd[0]);
        if(dup2(pipefd[1], STDOUT_FILENO)<0){
            perror("Error haciendo el dup");
            exit(1);
        }
        close(pipefd[1]);
        char width_str[16], height_str[16];
        if (snprintf(width_str, sizeof(width_str), "%d", config->width) < 0 ||
            snprintf(height_str, sizeof(height_str), "%d", config->height) < 0) {
            perror("Error formateando argumentos");
            exit(1);
        }

        execl(player_path, player_path, width_str, height_str, NULL);
        perror("Error haciendo el execl");//no deberia llegar
        exit(1);
    }
    close(pipefd[1]);

    players[player_id].pid = pid;
    players[player_id].pipe_fd = pipefd[0];
    players[player_id].active = true;
    game_state->players[player_id].pid = pid;
    
    return pid;
}

pid_t create_view_process(const char* view_path, master_config_t *config){
    int pid = fork();
    if(pid <0){
        perror("Error al crear proceso");
        return -1;
    }else if(pid == 0){
        char width_str[16], height_str[16];
        if (snprintf(width_str, sizeof(width_str), "%d", config->width) < 0 ||
            snprintf(height_str, sizeof(height_str), "%d", config->height) < 0) {
            perror("Error formateando argumentos");
            exit(1);
        }
        
        execl(view_path, view_path, width_str, height_str, NULL);
        perror("Error haciendo el execl");//no deberia llegar
        exit(1);
    }

}

void signal_handler(int sig __attribute__((unused))) {
    cleanup_resources();
    exit(1);
}

bool is_valid_move(int player_id, unsigned char move){
    return game_state->board[]
}


void game_loop(master_config_t *config) {
    time_t last_move = time(NULL);
    while (!game_state->is_game_over) {
        if(time(NULL)-last_move>config->turn_timeout) {
            //printf("Timeout alcanzado, finalizando juego\n");
            break;
        }

        bool processed_move=false;
        for(int tries=0; tries<config->player_count && !processed_move; tries++) {
            int id = (current_player + tries) % config->player_count;
                        
            if (!players[player_id].active || game_state->players[player_id].blocked) {
                continue;
            }

            unsigned char move;
            ssize_t bytes_read = read(players[player_id].pipe_fd, &move, 1);
            
            if (bytes_read == 0) {
                game_state->players[player_id].blocked = true;
                players[player_id].active = false;
                printf("Jugador %d bloqueado (EOF)\n", player_id);

            } else if (bytes_read == 1) {
                sem_wait(&game_sync->writer_mutex);
                sem_wait(&game_sync->state_mutex);
                
                if (is_valid_move(player_id, move)) {
                    apply_move(player_id, move);
                    last_valid_move = time(NULL);
                    printf("Jugador %d: movimiento válido %d\n", player_id, move);
                } else {
                    game_state->players[player_id].invalid_moves++;
                    printf("Jugador %d: movimiento inválido %d\n", player_id, move);
                }
                
                sem_post(&game_sync->state_mutex);
                sem_post(&game_sync->writer_mutex);
                
                sem_post(&game_sync->player_turn[player_id]);
                
                notify_view_and_wait();
                nanosleep(&delay_ts, NULL);
                
                processed_move = true;
            }
        }
    }

    sem_wait(&game_sync->state_mutex);
    game_state->game_finished = true;
    sem_post(&game_sync->state_mutex);
}

int main(int argc, char *argv[]){
    master_config_t config;
    //en caso de recibir una senal (como ctrl+c) se limpian los recursos
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    parser(&config, argc, argv);

    if(sup_shared_memory(&config) == -1) {
        fprintf(stderr, "Error al configurar la memoria compartida\n");
        //clear_resources();
        return 1;
    }
    for(int i=0; i<config.player_count; i++){
        if(cate_player_process(config.player_paths[i], i, &config) == -1){
            fprintf(stderr, "Error al crear proceso jugador %d\n", i);
            //clear_resources();
            return 1;
        }
    }

    if(config.view_path){
        view_pid = create_view_process(config.view_path, &config);
        if(vw_pid == -1){
            fprintf(stderr, "Error al crear proceso vista\n");
            //clear_resources();
            return 1;
        }
    }

    


    return 0;
}

