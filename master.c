#define _POSIX_C_SOURCE 200809L
#include "structs.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "game_functions.h"
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdbool.h>
#include <limits.h>
#include <semaphore.h>

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

// TENGO Q MOVER A GAME FUNCTIONS
int is_executable_file(const char *path) {
    if (!path) return 0;
    if (access(path, F_OK | X_OK) != 0) return 0; // exists & executable
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode); // regular file
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
    place_players_on_board(game_state);

    initialize_semaphores(game_sync, config->player_count);

   return 0;
}

void clear_resources(){
    int aux = game_state->player_count;        // cache
    cleanup_shared_memory(game_state, game_sync); // munmap here
    if (game_sync) {
        cleanup_semaphores(game_sync, aux);
    }
    
    if(state_shm_fd != -1){ 
        close(state_shm_fd);
        clear_shm(GAME_STATE_SHM);
    }
    if(sync_shm_fd != -1){
        close(sync_shm_fd);
        clear_shm(GAME_SYNC_SHM);
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

        if (!is_executable_file(player_path)) {
            perror("El player path no es valido");
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
    return pid;
}

void signal_handler(int sig __attribute__((unused))) {
    clear_resources();
    exit(1);
}

void notify_view_and_wait() {
    if (view_pid > 0) {
        sem_post(&game_sync->view_notify);
        sem_wait(&game_sync->view_done);
    }
}

void game_loop(master_config_t *config) {
    fd_set read_fds;
    struct timeval timeout;
    int max_fd = 0;
    for(int i = 0; i < config->player_count; i++) {
        if (players[i].pipe_fd > max_fd) {
            max_fd = players[i].pipe_fd;
        }
    }
    notify_view_and_wait();
    struct timespec delay_ts = {
        .tv_sec = config->delay / 1000,
        .tv_nsec = (config->delay % 1000) * 1000000
    };
    nanosleep(&delay_ts, NULL);

    int current_player = 0;
    time_t last_move = time(NULL);
    while (!game_state->is_game_over) {
        if(time(NULL)-last_move>config->timeout) {
            break;
        }

        FD_ZERO(&read_fds);
        bool any_active = false;
        for(int i = 0; i < config->player_count; i++) {
            if (players[i].active && !game_state->players[i].blocked) {
                FD_SET(players[i].pipe_fd, &read_fds);
                any_active = true;
            }
        }
        if(!any_active){
            break;
        }
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if(ready == -1){
            if (errno == EINTR) {
                continue;
                perror("Error en select");
                break;
            }
        }
        if (ready == 0) {
            continue; 
        }

        bool processed_move = false;
        for(int tries=0; tries<config->player_count && !processed_move; tries++) {
            int id = (current_player + tries) % config->player_count;
                        
            if (!players[id].active || game_state->players[id].blocked) {
                continue;
            }
            
            if(FD_ISSET(players[id].pipe_fd, &read_fds) == 0) {
                unsigned char move;
                ssize_t bytes_read = read(players[id].pipe_fd, &move, 1);
                if (bytes_read == 0) { // NO SE SI ESTO CONTEMPLA EL CASO DE QUE TENGA MOVIMIENTOS PERO TARDE
                    game_state->players[id].blocked = true;
                    players[id].active = false;

                } else if (bytes_read == 1) {
                    sem_wait(&game_sync->writer_mutex);
                    sem_wait(&game_sync->state_mutex);
                    
                    if (is_valid_move(game_state ,id ,move)) {
                        apply_move(game_state, id, move);
                        last_move = time(NULL);
                    } else {
                        game_state->players[id].invalid_moves++;
                    }
                    
                    sem_post(&game_sync->state_mutex);
                    sem_post(&game_sync->writer_mutex);
                    
                    sem_post(&game_sync->player_turn[id]);
                    
                    notify_view_and_wait();
                    nanosleep(&delay_ts, NULL);                
                    processed_move = true;
                }
            }
        }
    }

    sem_wait(&game_sync->state_mutex);
    game_state->is_game_over = true;
    sem_post(&game_sync->state_mutex);
}

void terminate_all_processes(master_config_t* config){
    printf("Terminando todos los procesos...\n");
    
    // Terminar jugadores
    for (int i = 0; i < config->player_count; i++) {
        if (players[i].pid > 0) {
            kill(players[i].pid, SIGTERM);
        }
    }
    
    // Terminar vista
    if (view_pid > 0) {
        kill(view_pid, SIGTERM);
    }
    
    // Dar tiempo para terminación graceful
    sleep(1);
    
    // Forzar terminación si es necesario
    for (int i = 0; i < config->player_count; i++) {
        if (players[i].pid > 0) {
            kill(players[i].pid, SIGKILL);
        }
    }
    
    if (view_pid > 0) {
        kill(view_pid, SIGKILL);
    }
}

void wait_for_processes(master_config_t* config){
    int status;
    
    printf("Esperando terminación de procesos...\n");
    
    // Esperar jugadores con timeout
    for(int i = 0; i < config->player_count; i++){
        if (players[i].pid > 0) {
            pid_t result = waitpid(players[i].pid, &status, WNOHANG);
            if (result == 0) {
                // Proceso aún corriendo, dar más tiempo
                sleep(1);
                result = waitpid(players[i].pid, &status, WNOHANG);
                if(result == 0){
                    printf("Forzando terminación del jugador %d\n", i);
                    kill(players[i].pid, SIGKILL);
                    waitpid(players[i].pid, &status, 0);
                }
            }
            
            if(WIFEXITED(status)){
                printf("Jugador %d terminó con código %d, puntaje: %u\n", i, WEXITSTATUS(status), game_state->players[i].score);
            }else if(WIFSIGNALED(status)){
                printf("Jugador %d terminó por señal %d, puntaje: %u\n", i, WTERMSIG(status), game_state->players[i].score);
            }
        }
    }
    
    // Esperar vista con timeout
    if(view_pid > 0){
        pid_t result = waitpid(view_pid, &status, WNOHANG);
        if(result == 0){
            sleep(1);
            result = waitpid(view_pid, &status, WNOHANG);
            if(result == 0){
                printf("Forzando terminación de la vista\n");
                kill(view_pid, SIGKILL);
                waitpid(view_pid, &status, 0);
            }
        }
        
        if(WIFEXITED(status)){
            printf("Vista terminó con código %d\n", WEXITSTATUS(status));
        }else if (WIFSIGNALED(status)){
            printf("Vista terminó por señal %d\n", WTERMSIG(status));
        }
    }
}

int main(int argc, char *argv[]){
    master_config_t config;
    //en caso de recibir una senal (como ctrl+c) se limpian los recursos
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    parser(&config, argc, argv);

    if(setup_shared_memory(&config) == -1) {
        fprintf(stderr, "Error al configurar la memoria compartida\n");
        clear_resources();
        return 1;
    }
    for(int i=0; i<config.player_count; i++){
        if(create_player_process(config.player_paths[i], i, &config) == -1){
            fprintf(stderr, "Error al crear proceso jugador %d\n", i);
            clear_resources();
            return 1;
        }
    }

    if(config.view_path){
        view_pid = create_view_process(config.view_path, &config);
        if(view_pid == -1){
            fprintf(stderr, "Error al crear proceso vista\n");
            clear_resources();
            return 1;
        }
    }

    initialize_semaphores(game_sync, config.player_count);

    game_loop(&config);

    terminate_all_processes(&config);

    wait_for_processes(&config);

    clear_resources();


    return 0;
}

