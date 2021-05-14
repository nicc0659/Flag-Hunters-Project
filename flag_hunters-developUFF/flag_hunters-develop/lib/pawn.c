#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>

#include "headers/debug.h"
#include "headers/models.h"
#include "headers/queue.h"
#include "headers/board.h"

typedef struct {
    int p_id;
    Objective obj;
    int write_buffer;
    int read_buffer;
} PawnState;

void _detach_from_shm(Shmdata *);
Square *_moving_following_path(Square *, Objective, int const *, Config *, Shmdata *, PawnState *, int, int, int);
Square *_move(Square , Square , Config *, Shmdata *, PawnState *);
int **_mat_from_shm(Config *, Shmdata *, Objective);
Node *a_star(Config *, int **, Square , Objective);
int *_path(Node *);
void debug_pawn(Config *, Shmdata *, int);
int nanosleep(const struct timespec*, struct timespec*);
void _new_obj(int, int, Square *);

int UP = 1;
int RIGHT = 2;
int DOWN = 3;
int LEFT = 4;

void pawn_init(Square square, int read_buffer, int write_buffer, Config *config, Shmdata *shmdata, int p_id) {
    char tmp_buffer[50];
    int c;
    PawnState *pawn_state = malloc(sizeof(PawnState));
    pawn_state->p_id = p_id;
    Square *s;
    for (;;) { 
        int pipe_status = read(read_buffer, &c, sizeof(int));
        switch (pipe_status) {
            case -1:
                if (errno == EAGAIN) {
                    fflush(stdout);
                } else {
                    perror("pipe switch");
                }
                break;
            
            case 0:
                fflush(stdout);
                sprintf(tmp_buffer, "Pawn %d, closing pipes.", getpid());
                debug_print(tmp_buffer);
                close(read_buffer);
                close(write_buffer);
                _detach_from_shm(shmdata);
                exit(0);
                break;

            default:
                if (c == 0) {
                    read(read_buffer, &pawn_state->obj, sizeof(Objective));
                    int m = 1;
                    write(write_buffer, &m, sizeof(int));
                }
                else if (c == ROUND_START) {
                    int **mat = _mat_from_shm(config, shmdata, pawn_state->obj);
                    Node *end = a_star(config, mat, square, pawn_state->obj);
                    int * path;
                    if (end != NULL)
                        path = _path(end);
                    s = _moving_following_path(&square, pawn_state->obj, path, config, shmdata, pawn_state, p_id, write_buffer, read_buffer);
                } else if (c == MSG_OBJ) {
                    _new_obj(write_buffer, read_buffer, s);
                }
                break;
        }
	sleep(1);
    }
    exit(0);
}

void _new_obj(int write_buffer, int read_buffer, Square *s) {
    write(write_buffer, s, sizeof(Square));
}

void _send_move(Shmdata * shmdata, PawnState *pawn_state) {
    Message m;
    m.mesg_type = getppid();
    sprintf(m.mesg_text, "10");
    if (msgsnd(shmdata->msgid, &m, (sizeof(Message)- sizeof(long)), 0) == -1) {
        perror("msgsnd pawn");
    }
}

Square *_moving_following_path(Square *square, Objective obj, int const *path, Config *config, Shmdata *shmdata, PawnState *pawn_state, int p_id, int write_buffer, int read_buffer) {
    int i = 0;
    int m = MSG_MOVE, r;
    Square *old = square;
    fcntl(read_buffer, F_SETFL, O_NONBLOCK);
    while (path[i] != 0 && obj.moves > 0) {
        if (read(read_buffer, &r, sizeof(int)) > 0)
            if (r == MSG_OBJ) {
                write(write_buffer, square, sizeof(Square));
                return square;
            }
        if (path [i] == UP) {
            if (!(square = _move(*square, 
                shmdata->shmaddress[((square->x) * config->SO_BASE) + (square->y+1)],
                config,
                shmdata, pawn_state
                ))) {
                // printf("Can't reach the flag\n");
                break;
            } else {
                _send_move(shmdata, pawn_state);
                obj.moves--;
            }
        } else if (path [i] == DOWN) {
            if (!(square = _move(*square, 
                shmdata->shmaddress[((square->x) * config->SO_BASE) + (square->y-1)],
                config,
                shmdata, pawn_state
                ))) {
                // printf("Can't reach the flag\n");
                break;
            } else {
                _send_move(shmdata, pawn_state);
                obj.moves--;
            }
        } else if (path [i] == RIGHT) { 
            if (!(square = _move(*square, 
                shmdata->shmaddress[((square->x+1) * config->SO_BASE) + (square->y)],
                config,
                shmdata, pawn_state
                ))) {
                // printf("Can't reach the flag\n");
                break;
            } else {
                _send_move(shmdata, pawn_state);
                obj.moves--;
            }
        } else if (path [i] == LEFT) { 
            if (!(square = _move(*square, 
                shmdata->shmaddress[((square->x-1) * config->SO_BASE) + (square->y)],
                config,
                shmdata, pawn_state
                ))) {
                // printf("Can't reach the flag\n");
                break;
            } else {
                _send_move(shmdata, pawn_state);
                obj.moves--;
            }
        }
        i++;
    }
    if (square && obj.flag_x == square->x && obj.flag_y == square->y) {
        // printf("flag got: %d\n", getppid());
        Message message;
        message.mesg_type = getppid();
        sprintf(message.mesg_text, "%d-%d", obj.flag_x, obj.flag_y);
        if (msgsnd(shmdata->msgid, &message, (sizeof(Message)- sizeof(long)), 0) == -1) {
            perror("msgsnd pawn");
        }
    }
    return square;
}

Square *_move(Square old_s, Square new_s, Config *config, Shmdata *shmdata, PawnState *pawn_state) {
    if (semctl(shmdata->semid, (new_s.x * config->SO_BASE) + (new_s.y), GETVAL) > 0 ||
        shmdata->shmaddress[(new_s.x * config->SO_BASE) + (new_s.y)].is_habited)
        return NULL;
    semctl(shmdata->semid, (old_s.x * config->SO_BASE) + (old_s.y), SETVAL, 1);
    semctl(shmdata->semid, (new_s.x * config->SO_BASE) + (new_s.y), SETVAL, 1);

    // Setting the new square
    shmdata->shmaddress[(new_s.x * config->SO_BASE) + (new_s.y)].is_habited = 1;
    shmdata->shmaddress[(new_s.x * config->SO_BASE) + (new_s.y)].is_habited_from_p_id = pawn_state->p_id;
    // Setting the old square
    shmdata->shmaddress[(old_s.x * config->SO_BASE) + (old_s.y)].is_habited = 0;
    shmdata->shmaddress[(old_s.x * config->SO_BASE) + (old_s.y)].is_visited = pawn_state->p_id;
    shmdata->shmaddress[(old_s.x * config->SO_BASE) + (old_s.y)].is_habited_from_p_id = 0;

    if (abs(old_s.x - new_s.x) > 1 || abs(old_s.y - new_s.y) > 1)
        return NULL;
    // else
        // printf("mov %d %d -> %d %d\n", old_s.x, old_s.y, new_s.x, new_s.y);

    semctl(shmdata->semid, (old_s.x * config->SO_BASE) + (old_s.y), SETVAL, 0);
    semctl(shmdata->semid, (new_s.x * config->SO_BASE) + (new_s.y), SETVAL, 0);

    struct timespec ts;
    ts.tv_nsec = config->SO_MIN_HOLD_NSEC;
    ts.tv_sec = 0;
    int i = nanosleep(&ts, &ts);

    return &shmdata->shmaddress[(new_s.x * config->SO_BASE) + (new_s.y)];
}

int coordinates_are_valid(int row, int col, Config *config, int **mat) { 
    // return true if row number and column number 
    // is in range 
    return (row >= 0) && (row < config->SO_BASE) && 
           (col >= 0) && (col < config->SO_ALTEZZA) &&
           mat[row][col] != 1; 
}

int x_supp[4] = {0, -1, 0, 1};
int y_supp[4] = {-1, 0, 1, 0};

Node *a_star(Config *config, int **mat, Square src, Objective dest) {
    node_t *open_list = NULL;
    node_t *closed_list = NULL;

    Node *n = malloc(sizeof(Node));
    n->s = src;
    n->f = 0;
    n->g = 0;
    n->h = 0;
    open_list = enqueue(open_list, n);

    while (open_list) {
        Node *curr = best_node(open_list);
        open_list = remove_node(open_list, curr);
        closed_list = enqueue(closed_list, curr);

        if (curr->s.x == dest.flag_x && curr->s.y == dest.flag_y) {
            return curr;
        }

        for (int i = 0; i < 4; i++) {
            int x = curr->s.x + x_supp[i];
            int y = curr->s.y + y_supp[i];
            Square s = {x, y, 0, 0, 0};
            Node child = {curr, s, 0, 0, 0};
            
            if (!coordinates_are_valid(x, y, config, mat) || exists(closed_list, child))
                continue;
            
            child.g = curr->g + 1;
            child.h = pow((child.s.x - dest.flag_x), 2) + pow((child.s.y - dest.flag_y), 2);
            child.f = child.g + child.h;

            if (exists_and_f_higher(open_list, child))
                continue;

            open_list = enqueue(open_list, &child);
        }
    }
    return NULL;
}

int _direction(Square p, Square q) {
    if (p.x == q.x && (p.y - q.y) == 1) return UP;
    if (p.x == q.x && (p.y - q.y) == -1) return DOWN;
    if (p.y == q.y && (p.x - q.x) == 1) return RIGHT;
    return LEFT;
}

int *_path(Node *child) {
    int *path = malloc(sizeof(int) * child->g+1);
    path[child->g] = 0;
    int i = child->g - 1;
    while (i >= 0) {
        if (child->parent)
            path[i] = _direction(child->s, child->parent->s);
        else
            path[i] = 0;
        child = child->parent;
        i--;
    }
    return path;
}

int **_mat_from_shm(Config *config, Shmdata *shmdata, Objective obj) {
    int **mat = (int **)malloc(config->SO_ALTEZZA * sizeof(int*));;
    for (size_t i = 0; i < config->SO_ALTEZZA; i++) {
        mat[i] = malloc(sizeof(int) * config->SO_BASE);
    }
    for (int i=0; i < config->SO_ALTEZZA; i++) {
        for (int o=0; o < config ->SO_BASE; o++) {
            Square square = shmdata->shmaddress[(i * config->SO_BASE) + o];
            if (square.is_habited || (square.is_flag && (square.x != obj.flag_x || square.y != obj.flag_y)))
                mat[i][o] = 1; 
            else
                mat[i][o] = 0; 
        }
    }
    return mat;
}

void _detach_from_shm(Shmdata *shmdata) {
    shmdt(shmdata->shmaddress);   
}

#define ANSI_EMPTY_CELL "\x1b[34m"
#define ANSI_FLAG "\x1b[31m"
#define ANSI_FLAG_TAKEN "\x1B[35m"
#define ANSI_VISITED "\x1B[36m"
#define ANSI_PLAYER "\x1b[32m"
#define ANSI_RESET "\x1b[0m"

void debug_pawn(Config *config, Shmdata *shmdata, int p_id) {
    FILE *f;
    char tmp_buffer[50];
    sprintf(tmp_buffer, "%d.log", p_id);
    f = fopen(tmp_buffer, "a+");
    if (f == NULL) {
        perror("opening log file");
        exit(1);
    }
    for (int i=0; i < config->SO_ALTEZZA; i++) {
        for (int o=0; o < config ->SO_BASE; o++) {
            Square square = shmdata->shmaddress[(i * config->SO_BASE) + o];
            if (square.is_flag && square.is_habited)
                fprintf(f, "%d ", square.is_habited_from_p_id);
            else if (square.is_flag)
                fprintf(f, "f ");
            else if (square.is_habited) {
                fprintf(f, "%d ", square.is_habited_from_p_id);
            }
            else if (square.is_visited >= 0)
                fprintf(f, "q ");
            else
                fprintf(f, "x ");
        }
        fprintf(f, "\n");
    }
    fprintf(f, "\n\n");
    fclose(f);
}
