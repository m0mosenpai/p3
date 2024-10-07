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
#define TOTAL_LOCALS 5
#define PATH "PATH"
#define DEFAULT_PATH "/bin"

// shell locals
/*static char *locals[TOTAL_LOCALS];*/


// shell built-ins
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


// shell return code
int shell_rc = -1;

char *fetch_if_var(char *token) {
    char *pre = "$";
    if (strncmp(pre, token, strlen(pre)) == 0) {
        char *tok = strtok(token, pre);

        char *val;
        if ((val = getenv(tok)) != NULL) {
            return val;
        }
    }
    return NULL;
}

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
    if (argc == 0 || argv == NULL) {
        fprintf(stderr, "wsh: cmd can't be empty!\n");
        return -1;
    }

    // handle vars, if any
    char *var_v = NULL;
    for (size_t i = 0; i < argc; i++) {
        // check for whole variables strings
        if ((var_v = fetch_if_var(argv[i])) != NULL) {
            argv[i] = realloc(argv[i], strlen(var_v) + 1);
            strcpy(argv[i], var_v);
            continue;
        }

        // check for variables in key/value pairs
        char *ptr = NULL;
        if ((ptr = strchr(argv[i], '=')) != NULL) {
            char delim[2] = "=";
            size_t cnt = 0;
            const size_t nvars = 2;
            size_t ttok_len = 0;
            char *token = strtok(argv[i], delim);
            char **tokens = malloc(nvars * sizeof(char*));
            while (token != NULL) {
                if (cnt > nvars-1) {
                    fprintf(stderr, "wsh: ignoring extra tokens while parsing key/value pair\n");
                    break;
                }

                ptr = NULL;
                var_v = NULL;
                if ((ptr = strchr(token, '$')) != NULL) {
                    if (cnt == 0) {
                        fprintf(stderr, "wsh: $ not allowed in variable names\n");
                        freev((void*)tokens, nvars-1, 1);
                        return -1;
                    }

                    if ((var_v = fetch_if_var(token)) != NULL) {
                        tokens[cnt] = malloc(strlen(var_v) + 1);
                        strcpy(tokens[cnt], var_v);
                        ttok_len += strlen(var_v);
                    }
                }

                if (var_v == NULL) {
                    tokens[cnt] = malloc(strlen(token) + 1);
                    strcpy(tokens[cnt], token);
                    ttok_len += strlen(token);
                }
                cnt++;
                token = strtok(NULL, delim);
            }

            argv[i] = realloc(argv[i], ttok_len + 1);
            snprintf(argv[i], ttok_len + 2, "%s=%s", tokens[0], tokens[1]);
            freev((void*)tokens, nvars-1, 1);
            continue;
        }
    }

    // 1. check if cmd is built-in
    for (size_t i = 0; i < TOTAL_BUILTINS; i++) {
        if (strcmp(argv[0], builtins[i]) == 0) {
            return ((*builtin_fn[i])(argc, argv));
        }
    }
    // 2. check if cmd is a full-path to executable
    if (access(argv[0], X_OK) == 0) {
        return exec_in_new_proc(argv[0], argv);
    }

    // 3. check if cmd path can be found in $PATH
    char *path = getenv(PATH);
    if (path != NULL) {
        char delim[2] = ":";
        char *token = strtok(path, delim);
        while (token != NULL) {
            size_t pathlen = 0;
            pathlen = strlen(token) + strlen("/") + strlen(argv[0]) + 1;
            char *fullpath = malloc(pathlen);
            snprintf(fullpath, pathlen, "%s/%s", token, argv[0]);

            // ??? breaks out of loop on a failed access?
            // ??? PATH keeps getting overwritten
            if (access(fullpath, X_OK) == 0) {
                int rc =  exec_in_new_proc(fullpath, argv);
                free(fullpath);
                return rc;
            }
            free(fullpath);
            token = strtok(NULL, delim);
        }
    }

    fprintf(stderr, "wsh: invalid command\n");
    return -1;
}

int main(int argc, char *argv[]) {
    // intialize default PATH
    if (setenv(PATH, DEFAULT_PATH, 1) != 0) {
        fprintf(stderr, "setenv: failed to initialize PATH\n");
        shell_rc = -1;
        return shell_rc;
    }

    // batch mode
    if (argc == 2) {
        const char *scriptFile = argv[1];
        FILE *script = fopen(scriptFile, "r");
        if (script == NULL) {
            fprintf(stderr, "fopen: script file not found!\n");
            shell_rc = -1;
            return shell_rc;
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
            fprintf(stderr, "malloc: failed to allocate memory for token list\n");
            shell_rc = -1;
            return shell_rc;
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
                    fprintf(stderr, "malloc: failed to allocate memory for tokens\n");
                    freev((void*)tokenList, tokenListSize-1, 1);
                    free(line);
                    shell_rc = -1;
                    return shell_rc;
                }

                strcpy(tokenList[tokenCnt], token);
                tokenCnt++;
                token = strtok(NULL, delim);
            }
            shell_rc = exec_cmd(tokenCnt, tokenList);

            // free tokenized elements from list
            freev((void*)tokenList, tokenListSize-1, 0);
            printf("wsh> ");
        }
        freev((void*)tokenList, tokenListSize-1, 1);
        free(line);
    }
    else {
        fprintf(stderr, "Usage: %s <script>\n", argv[0]);
        shell_rc = -1;
    }
    
    return shell_rc;
}

// Usage: exit
int wsh_exit(size_t argc, char** args) {
    if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", args[0]);
        return -1;
    }
    freev((void*)args, argc-1, 1);
    // TO-FIX: possible memory leak ("line" in main loop)
    exit(shell_rc);
}

// Usage: cd <dir-path>
int wsh_cd(size_t argc, char** args) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <dir>\n", args[0]);
        return -1;
    }
    char *var_v = fetch_if_var(args[1]);
    if (chdir(var_v == NULL ? args[1] : var_v) != 0) {
        fprintf(stderr, "chdir: failed to cd\n");
        return -1;
    }
    return 0;
}

// Usage: export <name>=<val>
//        export <name>=$VAR
int wsh_export(size_t argc, char** args) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <var>=<val>\n", args[0]);
        return -1;
    }

    size_t cnt = 0;
    const size_t nvars = 2;
    char delim[2] = "=";
    char *token = strtok(args[1], delim);
    char **tokens = malloc(nvars * sizeof(char*));
    while (token != NULL) {
        tokens[cnt] = malloc(strlen(token) + 1);
        strcpy(tokens[cnt++], token);
        token = strtok(NULL, delim);
    }

    // should have atleast 2 tokens for key-val pair
    if (cnt < nvars) {
        fprintf(stderr, "export: key/value pair missing\n");
        freev((void*)tokens, nvars-1, 1);
        return -1;
    }

    int rc = 0;
    if ((rc = setenv(tokens[0], tokens[1], 1)) != 0) {
        fprintf(stderr, "export: failed to setenv\n");
        rc = -1;
    }
    freev((void*)tokens, nvars-1, 1);
    return rc;
}

// Usage: local <name>=<val>
//        local <name>=$VAR
int wsh_local(size_t argc, char** args) {
    if (argc != 2 || args == NULL) {
        fprintf(stderr, "Usage: %s <var>=<val>\n", args[0]);
        return -1;
    }

    size_t cnt = 0;
    const size_t nvars = 2;
    char delim[2] = "=";
    char *token = strtok(args[1], delim);
    char **tokens = malloc(nvars * sizeof(char*));
    while (token != NULL) {
        tokens[cnt] = malloc(strlen(token) + 1);
        strcpy(tokens[cnt++], token);
        token = strtok(NULL, delim);
    }

    // TO-DO: set value as empty if key/value pair is missing
    if (cnt < nvars) {
        fprintf(stderr, "local: key/value pair missing\n");
        freev((void*)tokens, nvars-1, 1);
        return -1;
    }

    // TO-DO: implement linked list
    size_t total_len = strlen(tokens[0]) + strlen("=") + strlen(tokens[1]) + 1;
    char *lkv_pair = malloc(total_len);
    snprintf(lkv_pair, total_len, "%s=%s", tokens[0], tokens[1]);

    freev((void*)tokens, nvars-1, 1);
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
    if (argc != 1 || args == NULL) {
        fprintf(stderr, "Usage: %s\n", args[0]);
        return -1;
    }

    char path[PATH_MAX];
    if (getcwd(path, sizeof(path)) == NULL) {
        fprintf(stderr, "ls: failed to getcwd\n");
        return -1;
    }

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
