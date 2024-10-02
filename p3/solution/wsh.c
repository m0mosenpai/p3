#define _GNU_SOURCE

#include "stdio.h"
#include "stdlib.h"
#include <string.h>
#include "wsh.h"

#define DELIM " "
#define MIN_INPUT_SIZE 16
#define MIN_TOKEN_LIST_SIZE 5
#define TOTAL_BUILTINS 7

typedef struct command {
    int id;
    int (*cmd)(char* params);
} command;

int (*wsh_exit)(void);
int (*wsh_cd)(char* dir);
int (*wsh_export)(char* var, char* val);
int (*wsh_local)(char* var, char* val);
int (*wsh_vars)(void);
int (*wsh_history)(void);
int (*wsh_ls)(void);

// free string/ string lists
void freev(void **ptr, int len, int free_seg) {
    if (len < 0) while (*ptr) {free(*ptr); *ptr++ = NULL;}
    else while (len) {free(ptr[len]); ptr[len--] = NULL;}
    if (free_seg) free(ptr);
}

// determine which program to run
int parse(char** tokenList, size_t tokenCnt) {
    /*printf("Count: %zu\n", tokenCnt);*/
    /*printf("Tokens: ");*/
    for (size_t i = 0; i < tokenCnt; i++) {
        printf("%s ", tokenList[i]);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    // batch mode
    if (argc == 2) {
        printf("BATCH MODE\n");
        const char *scriptFile = argv[1];
        FILE *script = fopen(scriptFile, "r");
        if (script == NULL) {
            perror("fopen: script file not found!\n");
            return -1;
        }

        /*char *line = NULL;*/
        /*size_t len = 0;*/
        /*ssize_t read;*/
        /*while ((read = getline(&line, &len, script)) != -1) {*/
        /*    // read and execute commands in file  */
        /*}*/
    }
    // interactive mode
    else if (argc == 1) {
        printf("INTERACTIVE MODE\n");
        printf("wsh> ");

        command builtin[TOTAL_BUILTINS];

        char **tokenList = malloc(MIN_TOKEN_LIST_SIZE * sizeof(char*));
        if (tokenList == NULL){
            perror("malloc: failed to allocate memory for token list\n");
            return -1;
        }

        char *line = NULL;
        size_t len = 0;
        ssize_t read;
        size_t tokenListSize = MIN_TOKEN_LIST_SIZE;
        size_t tokenCnt = 0;
        while ((read = getline(&line, &len, stdin)) != -1) {
            char *token = strtok(line, DELIM);
            tokenCnt = 0;
            while (token != NULL) {
                size_t tokenSize = strlen(token);
                if (tokenCnt >= tokenListSize) {
                    tokenListSize *= 2;
                    tokenList = reallocarray(tokenList, tokenListSize, sizeof(char*));
                }
                tokenList[tokenCnt] = malloc(tokenSize + 1);
                if (tokenList[tokenCnt] == NULL){
                    perror("malloc: failed to allocate memory for tokens\n");
                    freev((void*)tokenList, tokenListSize-1, 1);
                    free(line);
                    return -1;
                }

                strcpy(tokenList[tokenCnt], token);
                tokenCnt++;

                token = strtok(NULL, DELIM);
            }
            if (parse(tokenList, tokenCnt) != 0) {
                freev((void*)tokenList, tokenListSize-1, 1);
                free(line);
                break;
            }
            printf("wsh> ");
        }
        freev((void*)tokenList, tokenListSize-1, 1);
        free(line);
    }
    else {
        printf("Usage: %s <script>\n", argv[0]);
        return -1;
    }
    
    return 0;
}
