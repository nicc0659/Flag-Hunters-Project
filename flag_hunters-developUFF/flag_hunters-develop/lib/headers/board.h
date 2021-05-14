#ifndef __BOARD_H_
#define __BOARD_H_

#include "models.h"

Shmdata *init_board(Config *, State *);
void board_print(Config *, State *, Shmdata *);
void board_cleanup(Config *, Shmdata *);
Flag *_get_flag(State *, int, int);
int _get_score_left(State *);
Square *board_get_free_square(Config *, Shmdata *);
Square *board_get_f_square_s(Config *, Shmdata *);
void clean_before_darkness(Shmdata *);

#endif