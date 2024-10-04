#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/limits.h>
#include <sys/wait.h>
#include "wsh.h"

#define MIN_INPUT_SIZE 16
#define MIN_TOKEN_LIST_SIZE 5
#define TOTAL_BUILTINS 7

char *PATH = "/bin:";

static char *builtins[TOTAL_BUILTINS] = {
    EXIT,
    CD,
    EXPORT,
    LOCAL,
    VARS,
    HISTORY,
    LS
};

static int (*builtin_fn[TOTAL_BUILTINS])(size_t, char**) = {
    &wsh_exit,
    &wsh_cd,
    &wsh_export,
    &wsh_local,
    &wsh_vars,
    &wsh_history,
    &wsh_ls,
};

// free string/ string lists
void freev(void **ptr, int len, int free_seg) {
    if (len < 0) while (*ptr) {free(*ptr); *ptr++ = NULL;}
    else while (len) {free(ptr[len]); ptr[len--] = NULL;}
    if (free_seg) free(ptr);
}

int exec_in_new_proc(char *cmd, char **args) {
    pid_t child_pid, wpid;
    int status;
    child_pid = fork();
    if (child_pid == -1) return -1;
    else if (child_pid == 0) {
        if(execv(cmd, args) == -1) return -1;
    }
    else {
        do {
            wpid = waitpid(child_pid, &status, WUNTRACED);
            if (wpid == -1) return -1;
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
    return 0;
}

int exec_cmd(size_t argc, char **argv) {
    // 1. check if cmd is built-in
    if (argc == 0) return -1;
    for (size_t i = 0; i < TOTAL_BUILTINS; i++) {
        if (strcmp(argv[0], builtins[i]) == 0) {
            return ((*builtin_fn[i])(argc, argv));
        }
    }
    // 2. check if cmd full-path is valid
    if (access(argv[0], X_OK) == 0) {
        return exec_in_new_proc(argv[0], argv);
    }
    // TO-DO
    // 3. check paths in $PATH
    else {
        printf("Checking in $PATH\n");
        return 0;
    }
    // invalid command
    return -1;
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
        char delim[2] = " ";
        while ((read = getline(&line, &len, stdin)) != -1) {
            char *token = strtok(line, delim);
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
                token = strtok(NULL, delim);
            }
            if (exec_cmd(tokenCnt, tokenList) != 0) {
                freev((void*)tokenList, tokenListSize-1, 1);
                free(line);
                return -1;
            }
            // free tokenized elements from list
            freev((void*)tokenList, tokenListSize-1, 0);
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

// Usage: export <name>=<val> 
int wsh_export(size_t argc, char** args) {
    if (argc != 2) return -1;

    // guaranteed to have one var assignment in one line
    const size_t nvars = 2;
    char **tokens = malloc(nvars * sizeof(char*));
    char delim[2] = "=";
    char *token = strtok(args[1], delim);
    size_t cnt = 0;
    while (token != NULL) {
        if (cnt > nvars-1) return -1;
        tokens[cnt] = malloc(strlen(token) + 1);
        strcpy(tokens[cnt++], token);
        token = strtok(NULL, delim);
    }
    if (setenv(tokens[0], tokens[1], 1) != 0) return -1;
    freev((void*)tokens, nvars-1, 1);
    return 0;
}

int wsh_local(size_t argc, char** args) {
    if (argc != 2 || args == NULL) return -1;
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
