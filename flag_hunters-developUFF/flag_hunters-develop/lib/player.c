#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "headers/debug.h"
#include "headers/board.h"
#include "headers/models.h"
#include "headers/player.h"
#include "headers/pawn.h"

typedef struct player_state {
    int *p_pipes_to;
    int *p_pipes_from;
    int n_objs;
    Objective *objs;
    int *arr_pid_t;
} PlayerState;

static Shmdata *_attach_to_shm(Config *, State *);
static void _detach_from_shm(Shmdata *);
void _send_message_to_master(int, int);
Square *_place_pawns(Config *, Shmdata *, int);
PlayerState *_send_objective_to_pawns(State *, Config *, Shmdata *, Square *);
int _pawns_has_already_obj(Objective **, int, int);
void _send_message_to_pawns(PlayerState *, int);
void _send_objs_to_pawn(int, Objective *);
void _close_all_pawns_pipes(PlayerState *);
int _wait_for_pawns_ready(PlayerState *);
void _listener(PlayerState *, Shmdata *shmdata, int, int);

int p_id;


void player_init(int tmp_id, int read_buffer, int write_buffer, Config *config, Shmdata *shmdata, State *state) {
    int c[1];
    char tmp_buffer[50];
    Square *pawns;
    PlayerState *player_state;
    p_id = tmp_id;
    shmdata->msgid = msgget(getppid(), 0);
    sprintf(tmp_buffer, "Player %d initialized.", p_id);
    debug_print(tmp_buffer);
    for (;;) {
        int pipe_status = read(read_buffer, c, sizeof(int));
        if (pipe_status < 0) {
            perror("read player");
            exit(1);
        }
        switch (pipe_status) {
            case -1:
                if (errno == EAGAIN) {
                    printf("Pipe is empty");
                    fflush(stdout);
                    sleep(1);
                } else {
                    perror("pipe switch");
                }
                break;
            
            case 0:
                fflush(stdout);
                _detach_from_shm(shmdata);
                sprintf(tmp_buffer, "Player %d, closing pipes.", p_id);
                debug_print(tmp_buffer);
                _close_all_pawns_pipes(player_state);
                close(read_buffer);
                close(write_buffer);
                exit(0);
                break;
            default:
                switch (c[0]) {
                    case 0:
                        shmdata = _attach_to_shm(config, state);
                        sprintf(tmp_buffer, "Player %d, shm attached, address: %p.", p_id, shmdata->shmaddress);
                        debug_print(tmp_buffer);
                        break;

                    case 1:
                        sprintf(tmp_buffer, "Player %d, pawns placing.", p_id);
                        debug_print(tmp_buffer);
                        pawns = _place_pawns(config, shmdata, p_id);
                        _send_message_to_master(write_buffer, 1);
                        sprintf(tmp_buffer, "Player %d, pawns placed.", p_id);
                        debug_print(tmp_buffer);
                        break;

                    case 2:
                        // Receiving flags
                        {
                            int i = 0;
                            read(read_buffer, c, sizeof(int));
                            state->n_flags = c[0];
                            fflush(stdout);
                            state->flags = malloc(sizeof(Flag) * state->n_flags);
                            pipe_status = read(read_buffer, state->flags, sizeof(Flag) * state->n_flags);
                            player_state = _send_objective_to_pawns(state, config, shmdata, pawns);
                            if (_wait_for_pawns_ready(player_state)) {
                                int m = 1;  
                                write(write_buffer, &m, sizeof(int));
                            }
                            fflush(stdout);
                        }
                        break;
                    
                    case 4: //Fine Gioco, sto uccidendo tutti i processi figli ancora attivi, che ho salvato nell'array
                        for (int i=0; i<player_state->n_objs; i++) {
                            kill(player_state->arr_pid_t[i], SIGKILL);
                        }
			exit(0);
                        break;

                    case ROUND_START:
                        _send_message_to_pawns(player_state, ROUND_START);
                        _listener(player_state, shmdata, write_buffer, read_buffer);
                        break;

                    default:
                        break;
                }
                fflush(stdout);
                break;
        }
        sleep(1);
    }
    exit(0);
}

void _listener(PlayerState *player_state, Shmdata *shmdata, int write_buffer, int read_buffer) {
    char ms[10];
    int m;

    while (1) {
        if (read(read_buffer, &m, sizeof(int)) > 0) {
            if (m == MSG_FLAG) {
                if (read(read_buffer, ms, sizeof(Message)- sizeof(long)) > 0) {
                    int flag_x, flag_y;
                    sscanf(ms, "%d-%d", &flag_x, &flag_y);
                    for (int i = 0; i < player_state->n_objs; ++i) {
                        if (player_state->objs[i].flag_x == flag_x
                            && player_state->objs[i].flag_y == flag_y) {
                            int r = MSG_OBJ;
                            write(player_state->p_pipes_to[i], &r, sizeof(int));
                        }
                    }
                }
            }
        }
    }
}

int _wait_for_pawns_ready(PlayerState *state) {
    int i = 0;
    while (i < state->n_objs) {
        int c;
        int pipe_status = read(state->p_pipes_from[i], &c, sizeof(int));
        if (pipe_status > 0 && c == 1) {
            i++;
        } else if (pipe_status == 0) {
            printf("%d\n", pipe_status);
            perror("waiting pawns ready");
            return 0;
        }
    }
    return 1;
}

static Shmdata *_attach_to_shm(Config *config, State *state) {
    int shmid;
    key_t mem_key;
    mem_key = ftok(".", 'a');
    if ((shmid = shmget(mem_key, (sizeof(Square))*(config->SO_BASE)*(config->SO_ALTEZZA), 0666)) < 0) {
        perror("shmget player");
    }
    Square *shmaddress;
    if ((shmaddress = (Square *) shmat(shmid, NULL, 0)) == (Square *) -1) {
        perror("shmat player");
    }

    int semid = semget(mem_key, (config->SO_BASE)*(config->SO_ALTEZZA), 0666);

    Shmdata *shmdata = malloc(sizeof(Shmdata));
    shmdata->shmid = shmid;
    shmdata->shmaddress = shmaddress;
    shmdata->semid = semid;
    return shmdata;
}

static void _detach_from_shm(Shmdata *shmdata) {
    shmdt(shmdata->shmaddress);   
}

void _send_message_to_master(int write_buffer, int message) {
    write(write_buffer, &message, sizeof(int));
}

Square *_place_pawns(Config *config, Shmdata *shmdata, int p_id) {
    srand(time(NULL));
    Square *pawns = malloc(sizeof(Square) * config->SO_NUM_P);
    if (pawns == NULL)
        printf("errno %d\n", errno);
    for (int i=0; i < config->SO_NUM_P; i++) {
        Square *square = board_get_f_square_s(config, shmdata);
        if (semctl(shmdata->semid, (square->x * config->SO_BASE) + square->y, SETVAL, 1) < 0) {
            perror("place_pawns");
        }
        square->is_habited = 1;
        square->is_habited_from_p_id = p_id;
        square->is_visited = p_id;
        pawns[i] = *square;
        semctl(shmdata->semid, (square->x * config->SO_BASE) + square->y, SETVAL, 0);
    }
    return pawns;
}

PlayerState *_send_objective_to_pawns(State *state, Config *config, Shmdata *shmdata, Square *pawns) {
    Objective *objs[state->n_flags];
    for (size_t i = 0; i < state->n_flags; i++)
        objs[i] = malloc(sizeof(Objective) * config->SO_NUM_P);
    
    // Inserting objective entries
    for (size_t i = 0; i < state->n_flags; i++) {
        for (size_t j = 0; j < config->SO_NUM_P; j++) {
            objs[i][j].flag_x = state->flags[i].x;
            objs[i][j].flag_y = state->flags[i].y;
            objs[i][j].distance = abs(objs[i][j].flag_x - pawns[j].x) + abs(objs[i][j].flag_y - pawns[j].y);
            objs[i][j].moves = objs[i][j].distance+2;
            objs[i][j].pawn_id = j;
            objs[i][j].points = state->flags[i].points;
            objs[i][j].ratio = (double) objs[i][j].points / (double) objs[i][j].distance;
        }
    }
    Objective *selected_objs;
    selected_objs = calloc(config->SO_NUM_P, sizeof(Objective));
    int max = config->SO_N_MOVES;
    int n_objs = 0;
    for (size_t i = 0; i < state->n_flags; i++) {
        // Sorting objectives per ratio
        for (size_t o = 0; o < state->n_flags; o++) {
            int flag = 1;
            int j = 0;
            while (flag && j < config->SO_NUM_P) {
                flag = 0;
                for (size_t k = 0; k < config->SO_NUM_P-j-1; k++)
                    if (objs[o][k].ratio < objs[o][k+1].ratio) {
                        Objective app = objs[o][k];
                        objs[o][k] = objs[o][k+1];
                        objs[o][k+1] = app;
                        flag = 1;
                    }
                j++;
            }
        }
        // Sorting flags per ratio
        int flag = 1;
        int j = 0;
        while (flag && j+i < state->n_flags) {
            flag = 0;
            for (size_t k = i; k < state->n_flags-j-1; k++)
                if (objs[k][0].ratio < objs[k+1][0].ratio) {
                    Objective *app = objs[k];
                    objs[k] = objs[k+1];
                    objs[k+1] = app;
                    flag = 1;
                }
            j++;
        }
        // Greedy calculation
        flag = 0;
        if (objs[i][0].ratio != -1)
            if (max - objs[i][0].moves >= 0) { 
                max -= objs[i][0].moves;
                selected_objs[i] = objs[i][0];
                n_objs++;
                flag = 1;
            }
        if (!flag)
            break;
        for (size_t o = 0; o < state->n_flags; o++)
            for (size_t j = 0; j < config->SO_NUM_P; j++) {
                if (objs[o][j].pawn_id == selected_objs[i].pawn_id)
                    objs[o][j].ratio = -1;
            }
    }
    // Generating pawns
    char tmp_buffer[50];
    int o = 0;
    PlayerState *player_state = malloc(sizeof(PlayerState));
    player_state->p_pipes_to = malloc(sizeof(int) * config->SO_NUM_P);
    player_state->p_pipes_from = malloc(sizeof(int) * config->SO_NUM_P);
    player_state->arr_pid_t = malloc(sizeof(pid_t) * config->SO_NUM_P);
    player_state->n_objs = n_objs;
    player_state->objs = selected_objs;
    sprintf(tmp_buffer, "Process %d, targets found.", p_id);
    debug_print(tmp_buffer);
    while(o < player_state->n_objs) {
        if (selected_objs[o].distance == 0)
            continue;
        int tmp_p_pid;
        int p_pipe_to[2];
        int p_pipe_from[2];
        pipe(p_pipe_to);
        pipe(p_pipe_from);
        if (fcntl(p_pipe_from[0], F_SETFL, O_NONBLOCK) < 0) 
            perror("fcntl from pawns");
        switch (tmp_p_pid = fork()) {
            case -1:
                perror("p fork: ");
                debug_print("Error while creating a p process");
                break;
            
            case 0: //Inizializzazione figlio
                if (close(p_pipe_to[1]) < 0)
                    perror("closing pipe");
                if (close(p_pipe_from[0]) < 0)
                    perror("closing pipe");
                    pawn_init(pawns[selected_objs[o].pawn_id],
                    p_pipe_to[0], 
                    p_pipe_from[1],
                    config,
                    shmdata,
                    p_id
                );
                break;

            default: //Padre
                close(p_pipe_to[0]);
                close(p_pipe_from[1]);
                player_state->p_pipes_to[o] = p_pipe_to[1];
                player_state->p_pipes_from[o] = p_pipe_from[0];
                player_state->arr_pid_t[o] = tmp_p_pid; //Segnare nell'array processo appena creato
                o++;
                break;
        }
    }

    // Debugging objectives
    /*for (size_t i = 0; i < player_state->n_objs; i++) {
        if (selected_objs[i].distance != 0)
            printf("obj: %d %d --> %d %d, pid: %d\n", pawns[selected_objs[i].pawn_id].x, pawns[selected_objs[i].pawn_id].y, selected_objs[i].flag_x, selected_objs[i].flag_y, p_id);
        else i--;
    }*/

    // Sending objectives to pawns
    _send_message_to_pawns(player_state, 0);
    for (size_t i = 0; i < player_state->n_objs; i++) {
        write(player_state->p_pipes_to[i], &selected_objs[i], sizeof(Objective));
    }

    return player_state;
}

 int _pawns_has_already_obj(Objective **objs, int n_objs, int pawn_id) {
     for (size_t i = 0; i < n_objs; i++)
         if (objs[i][0].pawn_id == pawn_id) return 1;
     return 0;
 }

void _send_message_to_pawns(PlayerState *state, int message) {
    for (size_t i = 0; i < state->n_objs; i++) {
        write(state->p_pipes_to[i], &message, sizeof(int));
    }
}

void _send_objs_to_pawn(int write_buffer, Objective *obj) {
    write(write_buffer, obj, sizeof(Objective));
}

void _close_all_pawns_pipes(PlayerState *state) {
    for (size_t i = 0; i < state->n_objs; i++) {
        close(state->p_pipes_from[i]);
        close(state->p_pipes_to[i]);
    }
}
