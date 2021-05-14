#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#include "headers/models.h"
#include "headers/board.h"
#include "headers/debug.h"
#include "headers/game.h"

Config *request_config();

int main() {
    debug_reset();
    debug_print("Program launched.");

    Config *config = request_config();
    debug_print("Config successfully loaded.");

    int round = 1;
    for (;;) {
        printf("\n\n--- ROUND %d ---\n", round);
        if (!game_start(config))
            break;
        round++;
    }

    exit(0);
}

Config *request_config() {
    char file_name[20] = "";
    printf("Scegli una configurazione per la partita.\n");
    printf("# (config.txt) ");
    char *r = fgets(file_name, 20, stdin);
    if (r == NULL)
        perror("fget");
    if (strlen(file_name) <= 1) 
        strcpy(file_name, "config.txt");
    else {
        size_t len = strlen(file_name);
        if (len > 0 && file_name[len-1] == '\n')
            file_name[--len] = '\0';
    }

    Config *config = malloc(sizeof(Config));
    FILE* file = fopen(file_name, "r");
    if (file == NULL) {
        perror("Opening config file");
        printf("Il file specificato sembra inesistente.\n");
        exit(1);
    }

    fscanf(
     file,
     "%d %d %d %d %d %d %d %d %d %d",
     &config->SO_NUM_G, &config->SO_NUM_P, &config->SO_MAX_TIME,
     &config->SO_BASE, &config->SO_ALTEZZA, &config->SO_FLAG_MIN,
     &config->SO_FLAG_MAX, &config->SO_ROUND_SCORE, &config->SO_N_MOVES,
     &config->SO_MIN_HOLD_NSEC
     );
    fclose (file);
    return config;
}
