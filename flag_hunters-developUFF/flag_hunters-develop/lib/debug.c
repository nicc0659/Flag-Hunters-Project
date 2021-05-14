#ifndef STDIO_H
    #include <stdio.h>
#endif
#ifndef STDLIB_H
    #include <stdlib.h>
#endif
#ifndef STRING_H
    #include <string.h>
#endif

#include "headers/debug.h"

void debug_reset() {
    FILE *f;
    f = fopen("lastest.log", "w");
    fclose(f);
}

void debug_print(char *message) {
    FILE *f;
    f = fopen("lastest.log", "a+");
    if (f == NULL) {
        perror("opening log file");
        exit(1);
    }
    fprintf(f, "[DEBUG] %s\n", message);
    fclose(f);
}