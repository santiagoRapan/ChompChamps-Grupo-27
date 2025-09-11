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
#include <string.h>

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
#define MIN_X 60
#define MIN_Y 20

static WINDOW *board_win = NULL;
static WINDOW *status_win = NULL;

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
        return ERROR;
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
        init_pair(COLOR_PLAYER_7, COLOR_RED, COLOR_BLACK);
        init_pair(COLOR_PLAYER_8, COLOR_BLACK, COLOR_BLUE);
        
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
    if(max_y < MIN_Y || max_x < MIN_X) {
        endwin();
        fprintf(stderr, "Terminal demasiado pequeña. Tamaño minimo: 60x20\n");
        return ERROR;
    }

    // Ventana del tablero
    int board_height = max_y - 10;
    int board_width = (max_x * 2) / 3;
    int status_width = max_x - board_width - 3;

    board_win = newwin(board_height, board_width, 2, 1);
    status_win = newwin(board_height, status_width, 2, board_width + 2);
    //info_win = newwin(6, max_x - 2, board_height + 3, 1);
    
    if(!board_win || !status_win /*|| !info_win*/) {
        cleanup_view();
        fprintf(stderr, "Error al crear ventanas de vista\n");
        return ERROR;
    }

    // Permitir scrollear
    scrollok(status_win, TRUE);
    
    // Bordes
    box(board_win, 0, 0);
    box(status_win, 0, 0);
    //box(info_win, 0, 0);

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

    // Layout
    int board_start_y = 2;
    int board_start_x = 3;
    int max_board_width = win_width - 6;
    int max_board_height = win_height - 4;
    
    // Verificar que el tablero entre
    int required_width = game_state->width * 3 + 4;
    int required_height = game_state->height + 3;
    
    if (required_width > max_board_width || required_height > max_board_height) {
        mvwprintw(board_win, win_height/2, (win_width - 20)/2, "Board too large for window");
        return;
    }

    // Column headers
    mvwprintw(board_win, board_start_y, board_start_x, "   ");
    for (int x = 0; x < game_state->width; x++) {
        // Exactamente 3 fixed chars
        mvwprintw(board_win, board_start_y, board_start_x + 3 + x * 3, "%2d ", x);
    }

    // Rows
    for (int y = 0; y < game_state->height; y++) {
        // row index: 3 fixed chars
        mvwprintw(board_win, board_start_y + 1 + y, board_start_x, "%2d ", y);

        for (int x = 0; x < game_state->width; x++) {
            const int screen_x = board_start_x + 3 + x * 3;
            const int screen_y = board_start_y + 1 + y;

            // Hay un jugador en x,y?
            int player_at_pos = -1;
            for (unsigned int p = 0; p < game_state->player_count; p++) {
                if (game_state->players[p].x == x && game_state->players[p].y == y) {
                    player_at_pos = (int)p;
                    break;
                }
            }

            if (player_at_pos >= 0) {
                char buf[4];
                snprintf(buf, sizeof(buf), "P%-2d", player_at_pos);
                wattron(board_win, COLOR_PAIR(COLOR_PLAYER_0 + (player_at_pos % 9)) | A_BOLD);
                mvwaddnstr(board_win, screen_y, screen_x, buf, 3);
                wattroff(board_win, COLOR_PAIR(COLOR_PLAYER_0 + (player_at_pos % 9)) | A_BOLD);
            } else {
                int cell_value = get_cell_value(game_state->board, x, y, game_state->width, game_state->height);
                if (cell_value > 0) {
                    wattron(board_win, COLOR_PAIR(COLOR_CELL_VALUE) | A_BOLD);
                    mvwprintw(board_win, screen_y, screen_x, "%-3d", cell_value);
                    wattroff(board_win, COLOR_PAIR(COLOR_CELL_VALUE) | A_BOLD);
                } else {
                    int playerid = -cell_value;
                    wattron(board_win, COLOR_PAIR(playerid)| A_REVERSE);
                    mvwprintw(board_win, screen_y, screen_x, "   ");
                    wattroff(board_win, COLOR_PAIR(playerid)| A_REVERSE);
                }
            }
        }
    }
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

void draw_complete_view(void){
    draw_window_titles();
    read_lock();
    draw_game_board();
    draw_player_status();
    read_unlock();

    // Usa wnoutrefresh y doupdate para minimizar parpadeo
    wnoutrefresh(board_win);
    wnoutrefresh(status_win);
    doupdate();

}

static void show_winner_banner(void) {
    int winner_idx;
    unsigned winner_score;

    read_lock();
    winner_idx   = determine_winner(game_state);
    winner_score = game_state->players[winner_idx].score;
    read_unlock();

    const char *title = "¡PARTIDA TERMINADA!";
    char line[128];
    snprintf(line, sizeof(line),
             "El ganador es el jugador %d con puntaje %u",
             winner_idx, winner_score);
    const char *hint = "Cerrando..."; // no pidas tecla si el master te va a cerrar

    int w = (int)strlen(title);
    int tmp = (int)strlen(line);  if (tmp > w) w = tmp;
    tmp = (int)strlen(hint);      if (tmp > w) w = tmp;
    w += 4;
    if (w > COLS - 2) w = COLS - 2;

    int h = 5;
    int y = (LINES - h) / 2;
    int x = (COLS - w) / 2;

    WINDOW *popup = newwin(h, w, y, x);
    wattron(popup, COLOR_PAIR(COLOR_FINISHED) | A_BOLD);
    box(popup, 0, 0);
    mvwprintw(popup, 1, (w - (int)strlen(title))/2, "%s", title);
    mvwprintw(popup, 2, (w - (int)strlen(line))/2,  "%s", line);
    wattroff(popup, COLOR_PAIR(COLOR_FINISHED) | A_BOLD);
    mvwprintw(popup, h - 2, (w - (int)strlen(hint))/2, "%s", hint);

    // draw popup on top of existing windows
    wnoutrefresh(board_win);
    wnoutrefresh(status_win);
    wrefresh(popup);

    // brief, non-blocking pause so it's visible before master proceeds
    napms(1200);   // ~1.2s (keep this < master's final wait)

    delwin(popup);
    // don't call endwin() here; just restore underlying content
    refresh();
}


int main(int argc, char* argv[]){
    if(argc != 3){
        fprintf(stderr, "Uso: %s <width> <height>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int width = atoi(argv[1]);
    int height = atoi(argv[2]);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    if (getenv("TERM") == NULL) setenv("TERM", "xterm-256color", 1);
    if (init_ncurses() != 0) {
        return EXIT_FAILURE;
    }

    game_state= setup_game_state(width,height);
    game_sync = setup_game_sync();
    if(game_state == NULL || game_sync == NULL){
        fprintf(stderr, "Error al inicializar el estado del juego o la sincronización\n");
        cleanup_view();
        return EXIT_FAILURE;
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
                show_winner_banner();
                sem_post(&game_sync->view_done);
                break;
            }
            continue; // Reintentar
        }
        
        draw_complete_view();
        
        read_lock();
        bool game_over = game_state->is_game_over;
        read_unlock();
        // Salir si el juego terminó
        if (game_over) {
            show_winner_banner();
            sem_post(&game_sync->view_done);
            break;

        }
        // Notificar al máster que se terminó de dibujar
        sem_post(&game_sync->view_done);
    }
    cleanup_view();
    return 0;
}