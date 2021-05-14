#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include "headers/models.h"
#include "headers/debug.h"
#include "headers/board.h"
#include "headers/game.h"
#include "headers/player.h"

void _send_message_to_players(State *, int, int);
void _send_message_to_player(State *, int, int);
void _close_all_players_pipes(State *, int);
int _game_round_start(Config *, Shmdata *, State *);
int _wait_for_players_ready(State *);
int _all_flags_are_taken(State *);

int game_start(Config *config) {
    int fd;
    char tmp_buffer[50];
    char tmp_pipe[10];
    State *state = malloc(sizeof(State));
    Shmdata *shmdata = init_board(config, state);
    // TODO: rename variables
    state->p_pids = malloc(sizeof(int[config->SO_NUM_G]));
    state->p_pipes_to = malloc(sizeof(int[config->SO_NUM_G]));
    state->p_pipes_from = malloc(sizeof(int[config->SO_NUM_G]));
    state->players = malloc(sizeof(Player) * config->SO_NUM_G);
    state->n_players = config->SO_NUM_G;
    for (int i = 0; i < config->SO_NUM_G; i++) {
        int tmp_g_pid;
        int p_pipe_to[2];
        int p_pipe_from[2];
        pipe(p_pipe_to);
        pipe(p_pipe_from);
        switch (tmp_g_pid = fork()) {
            case -1:
                perror("g fork: ");
                debug_print("Error while creating a g process");
                break;

            case 0:
                close(p_pipe_to[1]);
                close(p_pipe_from[0]);
                player_init(i, p_pipe_to[0], p_pipe_from[1], config, shmdata, state);
                break;
            
            default:
                close(p_pipe_to[0]);
                close(p_pipe_from[1]);
                state->p_pipes_to[i] = p_pipe_to[1];
                state->p_pipes_from[i] = p_pipe_from[0];
                state->p_pids[i] = tmp_g_pid;
                state->players[i].moves_left = config->SO_N_MOVES;
                state->players[i].pid = tmp_g_pid;
                sprintf(tmp_buffer, "Process player created with pid: %d", tmp_g_pid);
                debug_print(tmp_buffer);
                break;
        }
    }

    srand(time(NULL));

    for (int i = 0; i < config->SO_NUM_G; i++) {
        _send_message_to_player(state, i, 1);
        int flag = 1;
        while (flag) {
            char c[1];
            int pipe_status = read(state->p_pipes_from[i], c, sizeof(int));
            if (pipe_status < 0) {
                perror("debug");
                sleep(1);
            } else if (pipe_status == 0) {
                fflush(stdout);
            } else if (pipe_status > 0) {
                if (c[0] == 1)
                    flag = 0;
                fflush(stdout);
            }
        }
    }


    int r = _game_round_start(config, shmdata, state);

    board_print(config, state, shmdata);

    _close_all_players_pipes(state, config->SO_NUM_P);

    game_end(shmdata);
    return r;
}

void _send_message_to_player(State *state, int p_id, int message) {
    write(state->p_pipes_to[p_id], &message, sizeof(int));
}

void _send_message_to_players(State *state, int n_players, int message) {
    for (size_t i = 0; i < n_players; i++) {
        write(state->p_pipes_to[i], &message, sizeof(int));
    }
}

void _close_all_players_pipes(State *state, int n_players) {
    for (size_t i = 0; i < n_players; i++) {
        close(state->p_pipes_to[i]);
        close(state->p_pipes_from[i]);
    }
}

void end_game_message(State *state) { //Invio messaggio termine gioco
    _send_message_to_players(state, state->n_players, 4);
    for (int i=0; i<state->n_players; i++) {
        kill(state->players[i].pid, SIGKILL);
    }
}

int _game_round_start(Config *config, Shmdata *shmdata, State *state) {
    int n_flags;
    int score = config->SO_ROUND_SCORE;

    if (config->SO_FLAG_MAX - config->SO_FLAG_MIN)
        n_flags = (rand() % (config->SO_FLAG_MAX - config->SO_FLAG_MIN)) + config->SO_FLAG_MIN;
    else
        n_flags = config->SO_FLAG_MAX;
    
    state->flags = malloc(sizeof(Flag)*n_flags);
    state->n_flags = n_flags;
    
    // Master flag placement
    while (n_flags > 0) {
        Square *square = board_get_f_square_s(config, shmdata);
        if (square == NULL) {
            printf("This configuration has minimum amout of flags too high for the size of the grid\nExiting...");
            game_end(shmdata);
        }
        semctl(shmdata->semid, (square->x * config->SO_BASE) + square->y, SETVAL, 1);
        square->is_flag = 1;
        state->flags[n_flags-1].x = square->x;
        state->flags[n_flags-1].y = square->y;
        int flag_points = rand() % (score - n_flags + 1) + 1;
        score -= flag_points;
        state->flags[n_flags-1].points = flag_points;
        state->flags[n_flags-1].taken = 0;
        semctl(shmdata->semid, (square->x * config->SO_BASE) + square->y, SETVAL, 0);
        n_flags--;
    }

    board_print(config, state, shmdata);

    // Sending flags to players
    _send_message_to_players(state, config->SO_NUM_G, 2);
    _send_message_to_players(state, config->SO_NUM_G, state->n_flags);
    for (size_t i = 0; i < config->SO_NUM_G; i++)
        write(state->p_pipes_to[i], state->flags, sizeof(Flag) * state->n_flags);

    // Waiting for players to be ready
    if (_wait_for_players_ready(state) == 0)
        game_end(shmdata);
    debug_print("Players all ready.");

    // Sending ROUND_START signal
    _send_message_to_players(state, config->SO_NUM_G, ROUND_START);
    int start_time = (int) (clock() / CLOCKS_PER_SEC);

    // Waiting for updates
    while (1) {
        // Break if the max time has been reached
        if (((clock() / CLOCKS_PER_SEC) - start_time) >= config->SO_MAX_TIME) {
            end_game_message(state);
            return 0;
        }
        Message message;
        if (msgrcv(shmdata->msgid, &message, sizeof(Message), 0, IPC_NOWAIT) > 0) {
            int is_number = 1;
            for (int i = 0; i < strlen(message.mesg_text); i++)
                if (message.mesg_text[i] < '0' || message.mesg_text[i] > '9')
                    is_number = 0;
            if (is_number) {
                if (atoi(message.mesg_text) == MSG_MOVE) {
                    for (int i = 0; i < state->n_players; ++i) {
                        if (state->players[i].pid == message.mesg_type)
                            state->players[i].moves_left--;
                    }
                }
            } else {
                int flag_x, flag_y;
                sscanf(message.mesg_text, "%d-%d", &flag_x, &flag_y);
                for (int i = 0; i <state->n_flags; ++i) {
                    if (state->flags[i].x == flag_x && state->flags[i].y == flag_y)
                        state->flags[i].taken = 1;
                }
                for (int j = 0; j < state->n_players; ++j) {
                    int m = MSG_FLAG;
                    if (write(state->p_pipes_to[j], &m, sizeof(int)) < 0)
                        perror("write master");
                    if (write(state->p_pipes_to[j], message.mesg_text, sizeof(char) *10) < 0)
                        perror("write master");
                }
                if (_all_flags_are_taken(state)) {
                    end_game_message(state);
                    break;
                }
            }
        }
    }
    return 1;
}



int _all_flags_are_taken(State *state) {
    for (int i = 0; i < state->n_flags; ++i) {
        if (state->flags[i].taken != 1)
            return 0;
    }
    return 1;
}

int _wait_for_players_ready(State *state) {
    for (size_t i = 0; i < state->n_players; i++) {
        int c;
        int pipe_status = read(state->p_pipes_from[i], &c, sizeof(int));
        if (pipe_status < 0 || c != 1) {
            perror("waiting players ready");
            return 0;
        }
    }
    return 1;
}

void game_end(Shmdata *shmdata) {
    clean_before_darkness(shmdata);
}