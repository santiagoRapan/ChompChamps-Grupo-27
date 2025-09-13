#ifndef STRUCTS_H
#define STRUCTS_H

#define MAX_PLAYERS 9
#define GAME_STATE_SHM "/game_state"
#define GAME_SYNC_SHM "/game_sync"
#define MAX_NAME_LENGTH 16

#define NUM_DIRECTIONS 8
#define MIN_BOARD_SIZE 10
#define MAX_CELL_VALUE 9
#define MIN_CELL_VALUE 1
#define PLAYER_ID_OFFSET 1

#define INVALID_POSITION_SCORE -1000
#define OCCUPIED_CELL_SCORE -1000
#define BASE_REWARD_MULTIPLIER 10
#define CENTER_BONUS_MAX 20
#define MOBILITY_BONUS 5

#define MIN_TERMINAL_WIDTH 60
#define MIN_TERMINAL_HEIGHT 20
#define CELL_DISPLAY_WIDTH 3
#define BOARD_MARGIN 4
#define BOARD_HEADER_HEIGHT 3


#define ARG_BUFFER_SIZE 16

#define FINAL_VIEW_DISPLAY_MS 6000
#define VIEW_WAIT_TIMEOUT_SEC 2

#define MS_TO_SEC 1000
#define MS_TO_NS 1000000L
#define NS_PER_SEC 1000000000L

#define GRACEFUL_TERMINATION_WAIT_SEC 1

#define MOVE_DATA_SIZE 1

#define PLAYER_POSITION_MARGIN 1
#define PLAYER_POSITION_OFFSET 2

#define COLOR_PLAYER_0_PAIR 1
#define COLOR_PLAYER_1_PAIR 2
#define COLOR_PLAYER_2_PAIR 3
#define COLOR_PLAYER_3_PAIR 4
#define COLOR_PLAYER_4_PAIR 5
#define COLOR_PLAYER_5_PAIR 6
#define COLOR_PLAYER_6_PAIR 7
#define COLOR_PLAYER_7_PAIR 8
#define COLOR_PLAYER_8_PAIR 9
#define COLOR_CELL_VALUE_PAIR 10
#define COLOR_CAPTURED_PAIR 11
#define COLOR_BORDER_PAIR 12
#define COLOR_HEADER_PAIR 13
#define COLOR_FINISHED_PAIR 14

#define BOARD_WINDOW_Y_OFFSET 2
#define BOARD_WINDOW_X_OFFSET 1
#define STATUS_WINDOW_Y_OFFSET 2
#define STATUS_WINDOW_X_GAP 2
#define WINDOW_BOTTOM_MARGIN 10
#define BOARD_WIDTH_RATIO_NUM 2
#define BOARD_WIDTH_RATIO_DEN 3
#define TITLE_CENTER_OFFSET 35
#define WINNER_POPUP_HEIGHT 5
#define WINNER_POPUP_BORDER 4
#define VIEW_REFRESH_DELAY_MS 1200

#define SHM_PERMISSIONS 0644
#define SHM_CONNECT_PERMISSIONS 0

#define SEM_SHARED_PROCESS 1
#define SEM_INITIAL_VALUE_SIGNAL 0
#define SEM_INITIAL_VALUE_MUTEX 1
enum ErrorCodes {
    ERR_GENERIC = -1,
    ERR_PIPE    = -2,
    ERR_FORK    = -3,
    ERR_EXEC    = -4,
    ERR_SHM     = -5
};
#include <sys/types.h>
#include <semaphore.h>
#include <stdbool.h>

typedef struct {
    char name[MAX_NAME_LENGTH]; // Nombre del jugador
    unsigned int score; // Puntaje
    unsigned int invalid_moves; // Cantidad de solicitudes de movimientos inválidas realizadas
    unsigned int valid_moves; // Cantidad de solicitudes de movimientos válidas realizadas
    unsigned short x, y; // Coordenadas x e y en el tablero
    pid_t pid; // Identificador de proceso
    bool blocked; // Indica si el jugador está bloqueado
} player_t;

typedef struct {
    unsigned short width; // Ancho del tablero
    unsigned short height; // Alto del tablero
    unsigned int player_count; // Cantidad de jugadores
    player_t players[MAX_PLAYERS]; // Lista de jugadores
    bool is_game_over; // Indica si el juego se ha terminado
    int board[]; // Puntero al comienzo del tablero. fila-0, fila-1, ..., fila-n-1
} game_state_t;

typedef struct {
    sem_t view_notify; // El máster le indica a la vista que hay cambios por imprimir
    sem_t view_done; // La vista le indica al máster que terminó de imprimir
    sem_t writer_mutex; // Mutex para evitar inanición del máster al acceder al estado
    sem_t state_mutex; // Mutex para el estado del juego
    sem_t reader_count_mutex; // Mutex para la siguiente variable
    unsigned int readers_count; // Cantidad de jugadores leyendo el estado
    sem_t player_turn[MAX_PLAYERS]; // Le indican a cada jugador que puede enviar 1 movimiento
} game_sync_t;

typedef enum {
    MOVE_UP = 0,        // ↑
    MOVE_UP_RIGHT = 1,  // ↗
    MOVE_RIGHT = 2,     // →
    MOVE_DOWN_RIGHT = 3,// ↘
    MOVE_DOWN = 4,      // ↓
    MOVE_DOWN_LEFT = 5, // ↙
    MOVE_LEFT = 6,      // ←
    MOVE_UP_LEFT = 7    // ↖
} move_direction_t;

extern const int MOVE_DELTAS[NUM_DIRECTIONS][2];

#endif // STRUCTS_H
