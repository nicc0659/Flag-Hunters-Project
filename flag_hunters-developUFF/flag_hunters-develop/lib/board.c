#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "headers/debug.h"
#include "headers/models.h"
#include "headers/board.h"

Shmdata *init_board(Config *config, State *state) {
    // Setting shared memory
    char tmp_buffer[50];
    Shmdata *shmdata = malloc(sizeof(Shmdata));
    if ((shmdata->shmid = shmget(IPC_PRIVATE, (sizeof(Square))*(config->SO_BASE)*(config->SO_ALTEZZA), IPC_CREAT|0666)) == -1) {
        printf("%d\n", errno);
        perror("shmget");
    }
    sprintf (tmp_buffer, "SHM ID: %d", shmdata->shmid);
    debug_print(tmp_buffer);

    if ((shmdata->shmaddress = (Square *) shmat(shmdata->shmid, NULL, 0)) == (Square *) -1) {
        printf("%d\n", errno);
        perror("shmat");
    };
    sprintf (tmp_buffer, "SHM Address: %p", shmdata->shmaddress);
    debug_print(tmp_buffer);
    debug_print("SHM Successfully created.");

    // Setting semaphores
    if ((shmdata->semid = semget(IPC_PRIVATE, (config->SO_BASE)*(config->SO_ALTEZZA), IPC_CREAT | IPC_EXCL | 0600)) == -1) {
        perror("semget");
        debug_print("Error during semget");
        printf("%d\n", errno);
    }
    sprintf (tmp_buffer, "SEMID: %d", shmdata->semid);
    debug_print(tmp_buffer);

    // Setting messages queue
    if ((shmdata->msgid = msgget(getpid(), 0666 | IPC_CREAT)) == -1)
        perror("msgget");

    board_cleanup(config, shmdata);
    return shmdata;
}

void board_cleanup(Config *config, Shmdata *shmdata) {
    Semun *semun = malloc(sizeof(Semun));
    semun->array = malloc(sizeof(int) * config->SO_BASE*config->SO_ALTEZZA);
    for (int i = 0; i < config->SO_BASE*config->SO_ALTEZZA; i++) {
        shmdata->shmaddress[i].x = i / config->SO_BASE;
        shmdata->shmaddress[i].y = i % config->SO_BASE;
        shmdata->shmaddress[i].is_habited = 0;
        shmdata->shmaddress[i].is_visited = -1;
        shmdata->shmaddress[i].is_flag = 0;
        semun->array[i] = 0;
    }
    if (semctl(shmdata->semid, 0, SETALL, *semun) < 0)
        perror("semctl SETALL");
    else
        debug_print("Semaphores successfully reset.");
}

void clean_before_darkness(Shmdata *shmdata) {
    debug_print("Cleaning SHM...");
    if (shmdt(shmdata->shmaddress) == -1) {
        perror("shmdt");
    }
    if (shmctl(shmdata->shmid, IPC_RMID, 0) == -1) {
        perror("shmctl");
    }
    debug_print("SHM successfully cleaned...");
    if (semctl(shmdata->semid, 0, IPC_RMID) == -1) {
        perror("semctl");
    }
    debug_print("SEM successfully cleaned...");
}

#define ANSI_EMPTY_CELL "\x1b[34m"
#define ANSI_FLAG "\x1b[31m"
#define ANSI_FLAG_TAKEN "\x1B[35m"
#define ANSI_VISITED "\x1B[36m"
#define ANSI_PLAYER "\x1b[32m"
#define ANSI_RESET "\x1b[0m"

void board_print(Config *config, State *state, Shmdata *shmdata) {
    char tmp_buffer[50];
    printf("\n");
    for (int i=0; i < config->SO_ALTEZZA; i++) {
        for (int o=0; o < config ->SO_BASE; o++) {
            Square square = shmdata->shmaddress[(i * config->SO_BASE) + o];
            if (square.is_flag && square.is_habited)
                printf(ANSI_FLAG_TAKEN "%d " ANSI_RESET, square.is_habited_from_p_id);
            else if (square.is_flag)
                printf(ANSI_FLAG "%d " ANSI_RESET, _get_flag(state, square.x, square.y)->points);
            else if (square.is_habited) {
                sprintf(tmp_buffer, ANSI_PLAYER "%d " ANSI_RESET, square.is_habited_from_p_id);
                printf("%s", tmp_buffer);
            }
            else if (square.is_visited >= 0)
                printf(ANSI_VISITED "%d " ANSI_RESET, square.is_visited);
            else
                printf(ANSI_EMPTY_CELL "x " ANSI_RESET);
        }
        printf("\n");
    }
    printf("Score left: %d\n", _get_score_left(state));
    for (size_t i = 0; i < state->n_players; i++) {
        printf("Player %ld: %d moves left.\n", i, state->players[i].moves_left);
    }
}

Flag *_get_flag(State *state, int x, int y) {
    for (size_t i = 0; i < state->n_flags; i++) {
        if (state->flags[i].x == x && state->flags[i].y == y) return &state->flags[i];
    }
    return NULL;
}

int _get_score_left(State *state) {
    int score = 0;
    for (size_t i = 0; i < state->n_flags; i++)
        if (!state->flags[i].taken) score += state->flags[i].points;
    return score;
}

Square *board_get_free_square(Config *config, Shmdata *shmdata) {
    int x = rand() % config->SO_BASE;
    int y = rand() % config->SO_ALTEZZA;
    int s_i, i = s_i = (x * config->SO_BASE) + y;
    Square *square = &shmdata->shmaddress[i];
    if (square->is_habited || square->is_flag) {
        i++;
        while ((shmdata->shmaddress[i].is_flag || shmdata->shmaddress[i].is_habited)) {
            i++;
            if (i >= s_i)
                return NULL;
            if (i >= (config->SO_BASE*config->SO_ALTEZZA))
                i = 0;
        }
    }
    square = &shmdata->shmaddress[i];
    return square;
}

Square *board_get_f_square_s(Config * config, Shmdata *shmdata) {
    int x = rand() % config->SO_BASE;
    int y = rand() % config->SO_ALTEZZA;
    int s_i, i = s_i = (x * config->SO_BASE) + y;
    Square *square = &shmdata->shmaddress[i];
    int res;
    if (res = semctl(shmdata->semid, (x * config->SO_BASE) + y, GETVAL) || square->is_habited || square->is_flag) {
        int app = rand() % (config->SO_BASE * config->SO_ALTEZZA);
        if (i + app > config->SO_BASE * config->SO_ALTEZZA)
            i = (i + app) - config->SO_BASE * config->SO_ALTEZZA;
        else
            i += app;
        while (res = semctl(shmdata->semid, (x * config->SO_BASE) + y, GETVAL)
                     || shmdata->shmaddress[i].is_flag
                     || shmdata->shmaddress[i].is_habited) {
            int app = rand() % (config->SO_BASE * config->SO_ALTEZZA);
            if (i + app > config->SO_BASE * config->SO_ALTEZZA)
                i = (i + app) - config->SO_BASE * config->SO_ALTEZZA;
            else
                i = i + app;
        }
        square = &shmdata->shmaddress[i];
    }
    return square;
}