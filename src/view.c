#define _POSIX_C_SOURCE 200809L

#include <ncurses.h>
#include <stdio.h>
#include <semaphore.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include "structs.h"
#include "game_functions.h"
#include "ipc.h"
#include <errno.h>

#define COLOR_PLAYER_0 1
#define COLOR_PLAYER_1 2
#define COLOR_PLAYER_2 3
#define COLOR_PLAYER_3 4
#define COLOR_PLAYER_4 5
#define COLOR_PLAYER_5 6
#define COLOR_PLAYER_6 7
#define COLOR_PLAYER_7 8
#define COLOR_PLAYER_8 9
#define COLOR_CELL_VALUE 10
#define COLOR_CAPTURED 11
#define COLOR_BORDER 12
#define COLOR_HEADER 13
#define COLOR_FINISHED 14

static WINDOW *board_win = NULL;
static WINDOW *status_win = NULL;
static WINDOW *info_win = NULL;

static game_state_t* game_state = NULL;
static game_sync_t* game_sync = NULL;

static volatile sig_atomic_t running = 1;

void cleanup_view(void) {
    if(board_win) {
        delwin(board_win);
        board_win = NULL;
    }
    if(status_win) {
        delwin(status_win);
        status_win = NULL;
    }
    if(info_win) {
        delwin(info_win);
        info_win = NULL;
    }

    if(stdscr){
        endwin();
    }
    cleanup_shared_memory(game_state, game_sync);
}

void signal_handler(int sig) {
    (void)sig; // avoid unused parameter warning
    running = 0;
    cleanup_view();
    exit(0);
}

void read_lock(void) {
    sem_wait(&game_sync->writer_mutex);      // Prevenir master starvation
    sem_wait(&game_sync->reader_count_mutex); // Protejer reader counter
    game_sync->readers_count++;              // Incrementar reader count
    if (game_sync->readers_count == 1) {     // Primer lector?
        sem_wait(&game_sync->state_mutex);   // Bloquear master de leer 
    }
    sem_post(&game_sync->reader_count_mutex); // Release counter protection
    sem_post(&game_sync->writer_mutex);      // Permito otros lectores 
}

void read_unlock(void) {
    sem_wait(&game_sync->reader_count_mutex); // Protejer reader counter
    game_sync->readers_count--;              // Decrementar reader count
    if (game_sync->readers_count == 0) {     // Ultimo lector?
        sem_post(&game_sync->state_mutex);   // Permito master leer
    }
    sem_post(&game_sync->reader_count_mutex); // Release counter protection
}

int init_ncurses(void) {
    if(initscr() == NULL) {
        fprintf(stderr, "Error al inicializar ncurses\n");
        return -1;
    }
    cbreak();
    noecho();
    curs_set(0);
    
    if (has_colors()) {
        start_color();
        use_default_colors(); // Permite usar el color de fondo por defecto del terminal
        
        // Definir colores para jugadores
        init_pair(COLOR_PLAYER_0, COLOR_RED, COLOR_BLACK);
        init_pair(COLOR_PLAYER_1, COLOR_BLUE, COLOR_BLACK);
        init_pair(COLOR_PLAYER_2, COLOR_GREEN, COLOR_BLACK);
        init_pair(COLOR_PLAYER_3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COLOR_PLAYER_4, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(COLOR_PLAYER_5, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_PLAYER_6, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_PLAYER_7, COLOR_RED, COLOR_BLUE);
        init_pair(COLOR_PLAYER_8, COLOR_YELLOW, COLOR_BLUE);
        
        // Otros elementos
        init_pair(COLOR_CELL_VALUE, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_CAPTURED, COLOR_BLACK, COLOR_BLACK);
        init_pair(COLOR_BORDER, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_HEADER, COLOR_WHITE, COLOR_BLUE);
        init_pair(COLOR_FINISHED, COLOR_RED, COLOR_YELLOW);

    }
    
    // Create windows
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_x; // max_x intentionally unused here; silence compiler warning
    
    // Verificar tamaño mínimo
    if(max_y < 20 || max_x < 60) {
        endwin();
        fprintf(stderr, "Terminal demasiado pequeña. Tamaño minimo: 60x20\n");
        return -1;
    }

    // Ventana del tablero
    int board_height = max_y - 10;
    int board_width = (max_x * 2) / 3;
    int status_width = max_x - board_width - 3;

    board_win = newwin(board_height, board_width, 2, 1);
    status_win = newwin(board_height, status_width, 2, board_width + 2);
    info_win = newwin(6, max_x - 2, board_height + 3, 1);
    
    if(!board_win || !status_win || !info_win) {
        cleanup_view();
        fprintf(stderr, "Error al crear ventanas de vista\n");
        return -1;
    }

    // Permitir scrollear
    scrollok(status_win, TRUE);
    
    // Bordes
    box(board_win, 0, 0);
    box(status_win, 0, 0);
    box(info_win, 0, 0);

    refresh();
    return 0;
}

void draw_window_titles(void) {
    // Titulo principal
    attron(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    mvprintw(0, (COLS - 35) / 2, "=== ChompChamps - Game View ===");
    attroff(COLOR_PAIR(COLOR_HEADER) | A_BOLD);
    
    // Titulos de ventanas
    mvprintw(1, 2, "Game Board");
    mvprintw(1, (COLS * 2) / 3 + 3, "Player Status");
    
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_x; // Evitar warning de unused variable
    mvprintw(max_y - 8, 2, "Game Information");
}

void draw_game_board(void){
    werase(board_win);
    box(board_win, 0, 0);
    
    int win_height, win_width;
    getmaxyx(board_win, win_height, win_width);

    // Calcular posicion relativa del tablero
    int board_start_y = 2;
    int board_start_x = 3;
    int max_board_width = win_width - 6;
    int max_board_height = win_height - 4;
    
    // Verificar que el tablero entre
    int required_width = game_state->width * 3 + 4;
    int required_height = game_state->height + 3;
    
    if(required_width > max_board_width || required_height > max_board_height) {
        mvwprintw(board_win, win_height/2, (win_width - 20)/2, "Board too large for window");
        wrefresh(board_win);
        return;
    }

    // Dibujar headers de columnas y filas
    mvwprintw(board_win, board_start_y, board_start_x, "   ");
    for (int x = 0; x < game_state->width; x++) {
        mvwprintw(board_win, board_start_y, board_start_x + 3 + x * 3, "%2d ", x);
    }
    for (int y = 0; y < game_state->height; y++) {
        mvwprintw(board_win, board_start_y + 1 + y, board_start_x, "%2d ", y);
        
        for (int x = 0; x < game_state->width; x++) {
            int cell_value = get_cell_value(game_state, x, y);
            int screen_x = board_start_x + 3 + x * 3;
            int screen_y = board_start_y + 1 + y;
            
            int player_at_pos = -1;
            for (unsigned int p = 0; p < game_state->player_count; p++) {
                if (game_state->players[p].x == x && game_state->players[p].y == y) {
                    player_at_pos = (int)p;
                    break;
                }
            }
            
            if (player_at_pos >= 0) {
                // Jugador (Color, ID)
                wattron(board_win, COLOR_PAIR(player_at_pos) | A_BOLD);
                mvwprintw(board_win, screen_y, screen_x, "P%d", player_at_pos);
                wattroff(board_win, COLOR_PAIR(player_at_pos) | A_BOLD);
            } else if (cell_value > 0) {
                // Rerward cell
                wattron(board_win, COLOR_PAIR(COLOR_CELL_VALUE) | A_BOLD);
                mvwprintw(board_win, screen_y, screen_x, "%2d", cell_value);
                wattroff(board_win, COLOR_PAIR(COLOR_CELL_VALUE) | A_BOLD);
            } else {
                // Captured cell
                wattron(board_win, COLOR_PAIR(COLOR_CAPTURED));
                mvwprintw(board_win, screen_y, screen_x, "██");
                wattroff(board_win, COLOR_PAIR(COLOR_CAPTURED));
            }
        }
    }
    
    wrefresh(board_win);
}

void draw_player_status(void) {
    werase(status_win);
    box(status_win, 0, 0);
    
    int win_height, win_width;
    getmaxyx(status_win, win_height, win_width);
    (void)win_width; // Para avoider warning de unused variable
    
    mvwprintw(status_win, 1, 2, "Players (%u):", game_state->player_count);
    
    int line = 3;
    for (unsigned int i = 0; i < game_state->player_count && line < win_height - 2; i++) {
        
        // Player ID + Nombre
        wattron(status_win, COLOR_PAIR(COLOR_PLAYER_0 + i) | A_BOLD);
        mvwprintw(status_win, line, 2, "P%u: %s", i, game_state->players[i].name);
        wattroff(status_win, COLOR_PAIR(COLOR_PLAYER_0 + i) | A_BOLD);
        line++;
        
        // Player status
        mvwprintw(status_win, line, 4, "Pos: (%d,%d)", game_state->players[i].x, game_state->players[i].y); 
        line++;
        mvwprintw(status_win, line, 4, "Score: %u", game_state->players[i].score);
        line++;
        mvwprintw(status_win, line, 4, "Moves: %u/%u", 
                 game_state->players[i].valid_moves, 
                 game_state->players[i].valid_moves + game_state->players[i].invalid_moves);
        line++;
        
        if(game_state->players[i].blocked) {
            wattron(status_win, COLOR_PAIR(COLOR_FINISHED) | A_BOLD);
            mvwprintw(status_win, line, 4, "[BLOCKED]");
            wattroff(status_win, COLOR_PAIR(COLOR_FINISHED) | A_BOLD);
            line++;
        }
        line++; // Dejo espacio
    }
    
    wrefresh(status_win);
}

void draw_game_info(void) {
    werase(info_win);
    box(info_win, 0, 0);
    
    // Game status
    if (game_state->is_game_over) {
        wattron(info_win, COLOR_PAIR(COLOR_FINISHED) | A_BOLD | A_BLINK);
        mvwprintw(info_win, 1, 2, "GAME FINISHED!");
        wattroff(info_win, COLOR_PAIR(COLOR_FINISHED) | A_BOLD | A_BLINK);
        
        // Buscar ganador
        unsigned int max_score = 0;
        int winner = -1;
        for (unsigned int i = 0; i < game_state->player_count; i++) {
            if (game_state->players[i].score > max_score) {
                max_score = game_state->players[i].score;
                winner = (int)i;
            }
        }
        
        if (winner >= 0) {
            wattron(info_win, COLOR_PAIR(COLOR_PLAYER_0 + winner) | A_BOLD);
            mvwprintw(info_win, 2, 2, "Ganador: Player %d (%s) - %u puntos!",
                     winner, game_state->players[winner].name, max_score);
            wattroff(info_win, COLOR_PAIR(COLOR_PLAYER_0 + winner) | A_BOLD);
        }
    } else {
        wattron(info_win, COLOR_PAIR(COLOR_HEADER));
        mvwprintw(info_win, 1, 2, "Juego en proceso...");
        wattroff(info_win, COLOR_PAIR(COLOR_HEADER));
    }
    
    //! Legend (QUIERO VER COMO SE VE ESTO)
    mvwprintw(info_win, 3, 2, "Legend: ");
    mvwprintw(info_win, 4, 4, "P0-P8: Players  ");
    wattron(info_win, COLOR_PAIR(COLOR_CELL_VALUE));
    wprintw(info_win, "1-9: Rewards  ");
    wattroff(info_win, COLOR_PAIR(COLOR_CELL_VALUE));
    wattron(info_win, COLOR_PAIR(COLOR_CAPTURED));
    wprintw(info_win, "██: Captured");
    wattroff(info_win, COLOR_PAIR(COLOR_CAPTURED));
    
    wrefresh(info_win);
}

void draw_complete_view(void){
    //clear();
    draw_window_titles();

    read_lock();
    draw_game_board();
    draw_player_status();
    draw_game_info();
    read_unlock();

    //Usa wnoutrefresh y doupdate para minimizar parpadeo
    wnoutrefresh(board_win);
    wnoutrefresh(status_win);
    wnoutrefresh(info_win);
    doupdate();
    //refresh();
}

int main(int argc, char* argv[]){
    if(argc != 3){
        fprintf(stderr, "Uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    int width = atoi(argv[1]);
    int height = atoi(argv[2]);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    if (getenv("TERM") == NULL) setenv("TERM", "xterm-256color", 1);
    if (init_ncurses() != 0) {
        return 1;
    }

    game_state= setup_game_state(width,height);
    game_sync = setup_game_sync();
    if(game_state == NULL || game_sync == NULL){
        fprintf(stderr, "Error al inicializar el estado del juego o la sincronización\n");
        cleanup_view();
        return 1;
    }

    // Mensaje inicial
    mvprintw(LINES/2, (COLS - 40)/3, "Vista conectada - Esperando actualizaciones del juego...");
    refresh();
    
    // Bucle principal de la vista
    while(running) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 2; // 2 segundos timeout
        
        // Esperar notificación del máster con timeout
        
        int sem_result = sem_timedwait(&game_sync->view_notify, &timeout);
        if (sem_result != 0) {
            read_lock();
            bool game_over = game_state->is_game_over;
            read_unlock();
            
            if (game_over) {
                sem_post(&game_sync->view_done);
                break;
            }
            continue; // Reintentar
        }
        
        draw_complete_view();
        
        // Notificar al máster que terminamos de imprimir
        sem_post(&game_sync->view_done);
        
        // Salir si el juego terminó
        if (game_state->is_game_over) {
            mvprintw(LINES - 1, 0, "Presiona cualquier tecla para salir...");
            refresh();
            timeout(2000); // 2 second timeout
            getch(); // Wait for key or timeout
            break;
        }
    }
    cleanup_view();
    return 0;
}