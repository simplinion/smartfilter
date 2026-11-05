#include "level3/level3.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int main(void)
{
    char *buffer = NULL;
    size_t capacity = 0;
    ssize_t length;
    while ((length = getline(&buffer, &capacity, stdin)) >= 0) {
        float scores[CATEGORY_COUNT];
        level3_model_scores((sf_line){.data = buffer, .length = (size_t)length}, scores);
        for (size_t i = 0; i < CATEGORY_FORMATTING; ++i)
            printf(i ? " %.9g" : "%.9g", (double)scores[i]);
        putchar('\n');
    }
    free(buffer);
    return ferror(stdin) || fflush(stdout) == EOF ? EXIT_FAILURE : EXIT_SUCCESS;
}
