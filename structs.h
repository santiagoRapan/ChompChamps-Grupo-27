#ifndef STRUCTS_H
#define STRUCTS_H

#define MAX_PLAYERS 9
#define GAME_STATE_SHM "/game_state"
#define GAME_SYNC_SHM "/game_sync"
#define MAX_NAME_LENGTH 16

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

#endif // STRUCTS_H
