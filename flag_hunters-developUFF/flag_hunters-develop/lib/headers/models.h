#ifndef __MODELS_H_
#define __MODELS_H_

typedef struct config
{
    int SO_NUM_G; // Numero di processi giocatore
    int SO_NUM_P; // Processi pedina per ogni giocatore
    int SO_MAX_TIME; // Tempo massimo per round (secs)
    int SO_BASE; // Dimensione base scacchiera
    int SO_ALTEZZA; // Dimensione altezza scacchiera
    int SO_FLAG_MIN; // Minimo numero di bandiere per round
    int SO_FLAG_MAX; // Massimo numero di bandiere per round
    int SO_ROUND_SCORE; // Punteggio totale assegnato alle bandierine per round
    int SO_N_MOVES; // Numero totale di mosse a disposizione delle pedine (per tutto il gioco)
    int SO_MIN_HOLD_NSEC; // Numero minimo di nanosecondi di occupazione di una cella da parte di una pedina
} Config;

typedef struct flag
{
    int x;
    int y;
    int points;
    int taken;
} Flag;

typedef struct square
{
    int x;
    int y;
    int is_habited;
    int is_visited;
    int is_habited_from_p_id;
    int is_flag;
} Square;

typedef struct player
{
    int moves_left;
    int pid;
} Player;


typedef struct state
{
    int *p_pids;
    int *p_pipes_to;
    int *p_pipes_from;
    Flag *flags;
    int n_flags;
    Player *players;
    int n_players;
} State;

typedef struct shmdata {
    int shmid;
    Square *shmaddress;
    int semid;
    int msgid;
} Shmdata;

typedef union semun {
    int              val;    /* Value for SETVAL */
    struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
    unsigned short  *array;  /* Array for GETALL, SETALL */
    struct seminfo  *__buf;  /* Buffer for IPC_INFO
                                (Linux-specific) */
} Semun;

typedef struct objective {
    int flag_x;
    int flag_y;
    int distance;
    int moves;
    int points;
    int pawn_id;
    double ratio;
} Objective;

typedef struct mesg_buffer {
    long mesg_type;
    char mesg_text[10];
} Message;

#define ROUND_START 3
#define MSG_MOVE 10
#define MSG_FLAG 11
#define MSG_OBJ 12
#define MSG_DONE 13
#define MSG_PROBLEM 14

#endif
