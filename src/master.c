#define _POSIX_C_SOURCE 200809L
#include "structs.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "game_functions.h"
#include "ipc.h"
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdbool.h>
#include <limits.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdatomic.h>

#define DEFAULT_WIDTH MIN_BOARD_SIZE
#define DEFAULT_HEIGHT MIN_BOARD_SIZE
#define DEFAULT_DELAY 200 //MILISEGUNDOS
#define DEFAULT_TIMEOUT 10

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
static player_process_t players[MAX_PLAYERS] = {0}; //evita hacerle kill a los jugadores inexistentes por ejemplo
static pid_t view_pid = -1;
static int state_shm_fd = -1;
static int sync_shm_fd = -1;
static volatile sig_atomic_t interrupted = 0; //para saber si hubo una señal de interrupcion

static inline bool all_players_blocked_or_inactive(const master_config_t* cfg) {
    for (int i = 0; i < cfg->player_count; i++) {
        if (players[i].active && !game_state->players[i].blocked) {
            return false;
        }
    }
    return true;
}

static void notify_view_and_wait_ms(long ms) {
    if (view_pid <= 0) return;
    sem_post(&game_sync->view_notify);

    //timed wait para evitar deadlocks si view muere
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += ms / MS_TO_SEC;
    ts.tv_nsec += (ms % MS_TO_SEC) * MS_TO_NS;
    if (ts.tv_nsec >= NS_PER_SEC) { ts.tv_sec++; ts.tv_nsec -= NS_PER_SEC; }

    while (sem_timedwait(&game_sync->view_done, &ts) == -1) {
        if (errno == EINTR) continue;  // reintentar si hubo interrupcion
        break;
    }
}

void clear_resources(){
    int count = game_state ? game_state->player_count : 0;
    if (game_sync) cleanup_semaphores(game_sync, count); //sem_destroy
    cleanup_shared_memory(game_state, game_sync); //detach
    
    if (state_shm_fd != -1){
        close(state_shm_fd); 
        clear_shm(GAME_STATE_SHM); //unlink
    }
    if (sync_shm_fd  != -1){ 
        close(sync_shm_fd);
        clear_shm(GAME_SYNC_SHM); //unlink
    }

    for (int i = 0; i < count; i++) {
        if (players[i].pipe_fd != -1){
            close(players[i].pipe_fd);
            players[i].pipe_fd = -1;
        }
    }
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
            if (config->width < MIN_BOARD_SIZE) config->width = MIN_BOARD_SIZE;
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            config->height = atoi(argv[++i]);
            if (config->height < MIN_BOARD_SIZE) config->height = MIN_BOARD_SIZE;
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
        exit(EXIT_FAILURE);
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

pid_t create_player_process(const char* player_path, int player_id, master_config_t* config){
    if (!is_executable_file(player_path)) {
            perror("El player path no es valido");
            return ERR_GENERIC;
    }

    int pipefd[2];
    if(pipe(pipefd) == -1){
        perror("Error al crear pipe");
        return ERR_PIPE;
    }

    pid_t pid = fork();
    if(pid == -1){
        perror("Error al crear proceso");
        close(pipefd[0]);
        close(pipefd[1]);
        return ERR_FORK;
    }

    if(pid == 0){
        close(pipefd[0]);
        if(dup2(pipefd[1], STDOUT_FILENO)<0){
            perror("Error haciendo el dup");
            close(pipefd[1]);
            exit(EXIT_FAILURE);
        }
        close(pipefd[1]);
        char width_str[ARG_BUFFER_SIZE], height_str[ARG_BUFFER_SIZE];
        if (snprintf(width_str, sizeof(width_str), "%d", config->width) < 0 || snprintf(height_str, sizeof(height_str), "%d", config->height) < 0){
            perror("Error formateando argumentos");
            exit(EXIT_FAILURE);
        }
        
        execl(player_path, player_path, width_str, height_str, NULL);
        perror("Error haciendo el execl");//no deberia llegar
        exit(EXIT_FAILURE);
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
        return ERR_FORK;
    }else if(pid == 0){
        char width_str[ARG_BUFFER_SIZE], height_str[ARG_BUFFER_SIZE];
        if (snprintf(width_str, sizeof(width_str), "%d", config->width) < 0 ||
            snprintf(height_str, sizeof(height_str), "%d", config->height) < 0) {
            perror("Error formateando argumentos");
            exit(EXIT_FAILURE);
        }
        
        execl(view_path, view_path, width_str, height_str, NULL);
        perror("Error haciendo el execl");//no deberia llegar
        exit(EXIT_FAILURE);
    }
    return pid;
}

void signal_handler(int sig __attribute__((unused))) {
    // Solo setea el flag y manda SIGTERM a los hijos
    interrupted = 1;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].pid > 0) kill(players[i].pid, SIGTERM);
    }
    if (view_pid > 0) kill(view_pid, SIGTERM);
}


static void game_loop(master_config_t *config) {
    fd_set read_fds;
    struct timeval timeout;
    int max_fd = 0;
    for(int i = 0; i < config->player_count; i++) {
        if (players[i].pipe_fd > max_fd) {
            max_fd = players[i].pipe_fd;
        }
    }
    
    notify_view_and_wait_ms(config->delay);

    struct timespec delay_ts = {
        .tv_sec  = config->delay / MS_TO_SEC,
        .tv_nsec = (config->delay % MS_TO_SEC) * MS_TO_NS
    };

    int current_player = 0;
    time_t last_move = time(NULL);


    while (!game_state->is_game_over) {
        if(interrupted){
            printf("Señal de terminacion detectada, limpiando...\n");
            break;
        }

        if(time(NULL) - last_move > config->timeout) {
            break;
        }

        if (all_players_blocked_or_inactive(config)) {
            break;
        }

        FD_ZERO(&read_fds);
        
        for(int i = 0; i < config->player_count; i++) {
            if (players[i].active && !game_state->players[i].blocked) {
                FD_SET(players[i].pipe_fd, &read_fds);
            }
        }
        
        timeout.tv_sec = config->timeout;
        timeout.tv_usec = 0;

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if(ready == -1){
            if (errno == EINTR) {
                if(interrupted) {
                    break;
                }
                continue;
            }
                perror("Error en select");
                break;
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
            if (!FD_ISSET(players[id].pipe_fd, &read_fds)) {
                continue;
            }

            unsigned char move;
            ssize_t n = read(players[id].pipe_fd, &move, 1);

            if (n == 0) { // EOF: el jugador termino
                game_state->players[id].blocked = true;
                players[id].active = false;
                close(players[id].pipe_fd);
                players[id].pipe_fd = -1;
                continue;
            }
            if (n < 0) {
                if (errno == EINTR) continue; //?
                // actuo como si el jugador se fue
                game_state->players[id].blocked = true;
                players[id].active = false;
                close(players[id].pipe_fd);
                players[id].pipe_fd = -1;
                continue;
            }
            
            sem_wait(&game_sync->writer_mutex);
            sem_wait(&game_sync->state_mutex);

            if (is_valid_move(game_state->board, move, game_state->players[id].x, game_state->players[id].y, game_state->players[id].blocked, game_state->width, game_state->height)) {
                apply_move(game_state, id, move); //apply move icrementa valid_moves
                last_move = time(NULL);
            } else {
                game_state->players[id].invalid_moves++;
            }

            sem_post(&game_sync->state_mutex);
            sem_post(&game_sync->writer_mutex);

            sem_post(&game_sync->player_turn[id]);   // le permite al jugador hacer su movimiento

            notify_view_and_wait_ms(config->delay);
            nanosleep(&delay_ts, NULL);

            processed_move = true;
            current_player = (id + 1) % config->player_count;
        }
    }

    sem_wait(&game_sync->state_mutex);
    game_state->is_game_over = true;
    sem_post(&game_sync->state_mutex);

    notify_view_and_wait_ms(FINAL_VIEW_DISPLAY_MS);
}

void terminate_all_processes(master_config_t* config){
    printf("Terminando todos los procesos...\n");
    
    // Terminar jugadores
    for (int i = 0; i < config->player_count; i++) {
        if (players[i].pid > 0) {
            kill(players[i].pid, SIGTERM);
            players[i].active = false;
        }
    }
    
    // Terminar vista
    if (view_pid > 0) {
        kill(view_pid, SIGTERM);
    }
    
    // Dar tiempo para terminación graceful
    sleep(GRACEFUL_TERMINATION_WAIT_SEC);
    
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
    int exit_code = EXIT_SUCCESS;
    master_config_t config;
    //en caso de recibir una senal para terminar el proceso (como ctrl+c) hay que limpiar los recursos
    signal(SIGINT, signal_handler); //ctrl+c
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler); //ctrl+\, pero deberia ser contemplada
    signal(SIGHUP,  signal_handler); //en caso de cerrar la terminal repentinamente

    parser(&config, argc, argv);

    if(setup_shared_memory(&config) == -1) {
        fprintf(stderr, "Error al configurar la memoria compartida\n");
        exit_code = EXIT_FAILURE;
        goto clear;
    }
    for(int i=0; i<config.player_count; i++){
        if(create_player_process(config.player_paths[i], i, &config) == -1){
            fprintf(stderr, "Error al crear proceso jugador %d\n", i);
            exit_code = EXIT_FAILURE;
            goto clear;
        }
    }

    if(config.view_path){
        view_pid = create_view_process(config.view_path, &config);
        if(view_pid == -1){
            fprintf(stderr, "Error al crear proceso vista\n");
            exit_code = EXIT_FAILURE;
            goto clear;
        }
    }
    game_loop(&config);

    clear:
    terminate_all_processes(&config);

    wait_for_processes(&config);

    if(interrupted) {
        exit_code = EXIT_FAILURE;
    }
    return exit_code;
}

