#include <linux/limits.h>
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include "wsh.h"

#define DELIM " "
#define MIN_INPUT_SIZE 16
#define MIN_TOKEN_LIST_SIZE 5
#define TOTAL_BUILTINS 7

int exec_cmd(size_t argc, char **args) {
    /*printf("INSIDE exec_cmd\n");*/
    /*printf("argc: %zu\n", argc);*/
    /*for (size_t i = 0; i < argc; i ++) {*/
    /*    printf("%s ", args[i]);*/
    /*}*/
    /*printf("\n");*/
    char *builtins[TOTAL_BUILTINS] = {
        EXIT,
        CD,
        EXPORT,
        LOCAL,
        VARS,
        HISTORY,
        LS
    };

    int (*builtin_fn[TOTAL_BUILTINS])(size_t, char**) = {
        &wsh_exit,
        &wsh_cd,
        &wsh_export,
        &wsh_local,
        &wsh_vars,
        &wsh_history,
        &wsh_ls,
    };

    size_t i = 0;
    if (argc == 0) return -1;
    for (; i < TOTAL_BUILTINS; i++) {
        /*printf("Len: %zu, Builtin: %s\n", strlen(builtins[i]), builtins[i]);*/
        /*printf("Len: %zu, Cmd: %s\n", strlen(args[0]), builtins[i]);*/
        if (strcmp(args[0], builtins[i]) == 0) {
            /*printf("%s Matched!\n", builtins[i]);*/
            return ((*builtin_fn[i])(argc, args));
        }
    }
    return -1;
}

// free string/ string lists
void freev(void **ptr, int len, int free_seg) {
    if (len < 0) while (*ptr) {free(*ptr); *ptr++ = NULL;}
    else while (len) {free(ptr[len]); ptr[len--] = NULL;}
    if (free_seg) free(ptr);
}


int main(int argc, char *argv[]) {
    // batch mode
    if (argc == 2) {
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
        printf("wsh> ");

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
                // strip newline char
                char *ptr = NULL;
                if ((ptr = strchr(token, '\n'))) *ptr = '\0';

                size_t tokenSize = strlen(token);
                if (tokenCnt >= tokenListSize) {
                    tokenListSize *= 2;
                    tokenList = reallocarray(tokenList, tokenListSize, sizeof(char*));
                }
                tokenList[tokenCnt] = malloc(tokenSize + 1);
                if (tokenList[tokenCnt] == NULL) {
                    perror("malloc: failed to allocate memory for tokens\n");
                    freev((void*)tokenList, tokenListSize-1, 1);
                    free(line);
                    return -1;
                }

                strcpy(tokenList[tokenCnt], token);
                tokenCnt++;
                token = strtok(NULL, DELIM);
            }
            if (exec_cmd(tokenCnt, tokenList) != 0) {
                freev((void*)tokenList, tokenListSize-1, 1);
                free(line);
                return -1;
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

// Usage: exit
int wsh_exit(size_t argc, char** args) {
    if (argc != 1) return -1;
    freev((void*)args, argc-1, 1);
    exit(0);
}

// Usage: cd <dir-path>
int wsh_cd(size_t argc, char** args) {
    if (argc != 2) return -1;
    if (chdir(args[1]) != 0) return -1;
    return 0;
}

int wsh_export(size_t argc, char** args) {
    if (argc != 1 || args == NULL) return -1;
    return 0;
}

int wsh_local(size_t argc, char** args) {
    if (argc != 1 || args == NULL) return -1;
    return 0;
}

int wsh_vars(size_t argc, char** args) {
    if (argc != 1 || args == NULL) return -1;
    return 0;
}

int wsh_history(size_t argc, char** args) {
    if (argc != 1 || args == NULL) return -1;
    return 0;
}


// Usage: ls
int wsh_ls(size_t argc, char** args) {
    if (argc != 1 || args == NULL) return -1;

    char path[PATH_MAX];
    if (getcwd(path, sizeof(path)) == NULL) return -1;

    struct dirent **names;
    int n;
    n = scandir(path, &names, NULL, alphasort);
    int i = 0;
    char *pre = ".";
    while (i < n){
        if (strncmp(pre, names[i]->d_name, strlen(pre)) != 0)
            printf("%s\n", names[i]->d_name);
        free(names[i]);
        i++;
    }
    free(names);
    return 0;
}
