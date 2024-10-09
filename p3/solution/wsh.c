#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/limits.h>
#include <sys/wait.h>
#include <errno.h>
#include "wsh.h"

#define MIN_INPUT_SIZE 16
#define MIN_TOKEN_LIST_SIZE 5
#define TOTAL_BUILTINS 6
#define TOTAL_REDIRECTIONS 5
#define PATH "PATH"
#define DEFAULT_PATH "/bin"
#define DEFAULT_HISTORY_SIZE 5


// shell built-ins
// (EXIT is handled separately to manage memory)
static char *builtins[TOTAL_BUILTINS] = {
    CD,
    EXPORT,
    LOCAL,
    VARS,
    HISTORY,
    LS
};
static int (*builtin_fn[TOTAL_BUILTINS])(size_t, char**) = {
    &wsh_cd,
    &wsh_export,
    &wsh_local,
    &wsh_vars,
    &wsh_history,
    &wsh_ls,
};

// TO-DO: shell redirections
/*static char *redirects[TOTAL_REDIRECTIONS] = {*/
/*    R_IN,*/
/*    R_OUT,*/
/*    A_ROUT,*/
/*    R_ERROUT,*/
/*    A_RERROUT,*/
/*};*/
/*static int (*redirect_fn[TOTAL_REDIRECTIONS])(size_t, char**) = {*/
/*    &r_input,*/
/*    &r_output,*/
/*    &a_routput,*/
/*    &r_errout,*/
/*    &a_rerrout,*/
/*};*/

// free string lists
void freev(void **ptr, int len, int free_seg) {
    if (len < 0) while (*ptr) { free(*ptr); *ptr++ = NULL; }
    else { for (int i = 0; i < len; i++) free(ptr[i]); }
    if (free_seg) free(ptr);
}

// command history
typedef struct hentry {
    size_t idx;
    size_t argc;
    char **argv;
    struct hentry *next;
} hentry;
hentry *hhead = NULL;
size_t histsize = DEFAULT_HISTORY_SIZE;
size_t histentries = 0;

// free history memory
void free_history() {
    hentry *i = hhead;
    hentry *tmp = NULL;
    while (i != NULL) {
        freev((void*)i->argv, (i->argc), 1);
        tmp = i->next;
        free(i);
        i = tmp;
    }
}

// store in history
void log_cmd(size_t argc, char **argv) {
    if (histentries < histsize) {
        if (hhead == NULL) {
            hentry *newentry = malloc(sizeof(hentry));
            newentry->argc = argc;
            newentry->argv = malloc(argc * sizeof(char*));
            for (size_t i = 0; i < argc; i++) {
                newentry->argv[i] = malloc(strlen(argv[i]) + 1);
                strcpy(newentry->argv[i], argv[i]);
            }
            newentry->idx = 0;
            newentry->next = NULL;
            hhead = newentry;
            histentries++;
        }
        else {
            // check for consecutively repeating commands
            int is_repeated = 1;
            if (hhead->argc == argc) {
                for (size_t i = 0; i < argc; i++) {
                    if (hhead->argv[i] == NULL && argv[i] == NULL) continue;
                    if (hhead->argv[i] == NULL || argv[i] == NULL) { is_repeated = 0; break; }
                    if (strcmp(hhead->argv[i], argv[i]) != 0) {
                        is_repeated = 0;
                        break;
                    }
                }
            }
            else {
                is_repeated = 0;
            }

            if (!is_repeated) {
                hentry *newentry = malloc(sizeof(hentry));
                newentry->argc = argc;
                newentry->argv = malloc(argc * sizeof(char*));
                for (size_t i = 0; i < argc; i++) {
                    newentry->argv[i] = malloc(strlen(argv[i]) + 1);
                    strcpy(newentry->argv[i], argv[i]);
                }
                newentry->next = hhead;
                newentry->idx = hhead->idx+1;
                hhead = newentry;
                histentries++;
            }
        }
    }
}

// shell locals
typedef struct localvar {
    size_t idx;
    char *name;
    char *value;
    struct localvar *next;
} localvar;
localvar *lhead = NULL;

// free local var memory
void free_locals() {
    localvar *i = lhead;
    while (i != NULL) {
        free(i->name);
        free(i->value);
        localvar *tmp = i->next;
        free(i);
        i = tmp;
    }
}

// shell return code
int shell_rc = 0;

// fetch variable value, if exists
char *fetch_if_var(char *token) {
    char *pre = "$";
    if (strncmp(pre, token, strlen(pre)) == 0) {
        char *tok_dup = strdup(token);
        char *tok = strtok(tok_dup, pre);

        // 1. check env
        char *val;
        if ((val = getenv(tok)) != NULL) {
            free(tok_dup);
            return val;
        }

        // 2. check local
        localvar *i = lhead;
        while (i != NULL) {
            if (strcmp(tok, i->name) == 0) {
                free(tok_dup);
                return i->value;
            }
            i = i->next;
        }
        free(tok_dup);
    }
    return NULL;
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

// TO-DO: fix memory leaks
char **parse_cmd(size_t argc, char **argv) {
    // handle vars, if any
    char *var_v = NULL;
    for (size_t i = 0; i < argc; i++) {
        char *arg_dup = strdup(argv[i]);

        // check for whole variables strings
        if ((var_v = fetch_if_var(arg_dup)) != NULL) {
            argv[i] = realloc(argv[i], strlen(var_v) + 1);
            strcpy(argv[i], var_v);
            free(arg_dup);
            continue;
        }

        // check for variables in key/value pairs
        char *ptr = NULL;
        if ((ptr = strchr(arg_dup, '=')) != NULL) {
            char delim[2] = "=";
            size_t cnt = 0;
            const size_t nvars = 2;
            size_t ttok_len = 0;
            char *token = strtok(arg_dup, delim);
            char **tokens = malloc(nvars * sizeof(char*));
            while (token != NULL) {
                // TO-DO: test this condition
                // ignore any extra tokens
                if (cnt > nvars-1) break;

                ptr = NULL;
                var_v = NULL;
                if ((ptr = strchr(token, '$')) != NULL) {
                    if (cnt == 0) {
                        fprintf(stderr, "wsh: $ not allowed in variable names\n");
                        free(arg_dup);
                        freev((void*)tokens, nvars, 1);
                        return NULL;
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
            freev((void*)tokens, nvars, 1);
        }
        free(arg_dup);
    }
    return argv;
}

int exec_cmd(size_t argc, char **argv) {
    if (argc == 0 || argv == NULL) {
        fprintf(stderr, "wsh: cmd can't be empty!\n");
        return -1;
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
        char *path_dup = strdup(path);
        char delim[2] = ":";
        char *token = strtok(path_dup, delim);
        while (token != NULL) {
            size_t pathlen = 0;
            pathlen = strlen(token) + strlen("/") + strlen(argv[0]) + 1;
            char *fullpath = malloc(pathlen);
            snprintf(fullpath, pathlen, "%s/%s", token, argv[0]);

            if (access(fullpath, X_OK) == 0) {
                int rc =  exec_in_new_proc(fullpath, argv);
                free(path_dup);
                free(fullpath);
                return rc;
            }
            free(fullpath);
            token = strtok(NULL, delim);
        }
        free(path_dup);
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

    // TO-DO: batch mode
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

        char **tokens = calloc(MIN_TOKEN_LIST_SIZE, sizeof(char*));
        if (tokens == NULL){
            fprintf(stderr, "malloc: failed to allocate memory for token list\n");
            shell_rc = -1;
            return shell_rc;
        }

        char *line = NULL;
        size_t len = 0;
        ssize_t read;
        size_t ntoks = MIN_TOKEN_LIST_SIZE;
        size_t cnt = 0;
        char delim[2] = " ";
        while ((read = getline(&line, &len, stdin)) != -1) {
            char *token = strtok(line, delim);
            cnt = 0;
            while (token != NULL) {
                // strip newline char
                char *ptr = NULL;
                if ((ptr = strchr(token, '\n'))) *ptr = '\0';

                if (cnt >= ntoks) {
                    ntoks *= 2;
                    tokens = reallocarray(tokens, ntoks, sizeof(char*));
                }
                tokens[cnt] = malloc(strlen(token) + 1);
                if (tokens[cnt] == NULL) {
                    fprintf(stderr, "malloc: failed to allocate memory for tokens\n");
                    free(line);
                    freev((void*)tokens, ntoks, 1);
                    free_locals();
                    free_history();
                    shell_rc = -1;
                    return shell_rc;
                }

                strcpy(tokens[cnt], token);
                cnt++;
                token = strtok(NULL, delim);
            }
            // exit gracefully if user inputs "exit"
            if (strcmp(tokens[0], EXIT) == 0) {
                free(line);
                freev((void*)tokens, ntoks, 1);
                free_locals();
                free_history();
                wsh_exit(cnt, tokens);
            }

            // 1. log in history (excluding builtins)
            int isbuiltin = 0;
            for (size_t i = 0; i < TOTAL_BUILTINS; i++)
                if (strcmp(tokens[0], builtins[i]) == 0) { isbuiltin = 1; break; }
            if (!isbuiltin) log_cmd(cnt, tokens);

            // 2. parse
            char **parsed_tokens;
            if ((parsed_tokens = parse_cmd(cnt, tokens)) == NULL) {
                shell_rc = -1;
            }
            // 3. execute
            else { shell_rc = exec_cmd(cnt, parsed_tokens); }

            printf("wsh> ");
        }
        free(line);
        freev((void*)tokens, ntoks, 1);
    }
    else {
        fprintf(stderr, "Usage: %s <script>\n", argv[0]);
        shell_rc = -1;
    }
    
    free_locals();
    free_history();
    return shell_rc;
}

// Usage: exit
int wsh_exit(size_t argc, char** args) {
    if (argc != 1) {
        fprintf(stderr, "Usage: %s\n", args[0]);
        return -1;
    }
    /*freev((void*)args, argc, 1);*/
    /*free_locals();*/
    /*free_history();*/
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
    char *dup_arg = strdup(args[1]);
    char *token = strtok(dup_arg, delim);
    char **tokens = malloc(nvars * sizeof(char*));
    while (token != NULL) {
        tokens[cnt] = malloc(strlen(token) + 1);
        strcpy(tokens[cnt++], token);
        token = strtok(NULL, delim);
    }

    // should have atleast 2 tokens for key-val pair
    if (cnt < nvars) {
        fprintf(stderr, "export: key/value pair missing\n");
        freev((void*)tokens, nvars, 1);
        return -1;
    }

    int rc = 0;
    if ((rc = setenv(tokens[0], tokens[1], 1)) != 0) {
        fprintf(stderr, "export: failed to setenv\n");
        rc = -1;
    }
    free(dup_arg);
    freev((void*)tokens, nvars, 1);
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
    char *dup_arg = strdup(args[1]);
    char *token = strtok(dup_arg, delim);
    char **tokens = malloc(nvars * sizeof(char*));
    while (token != NULL) {
        tokens[cnt] = malloc(strlen(token) + 1);
        strcpy(tokens[cnt++], token);
        token = strtok(NULL, delim);
    }

    localvar *newvar = malloc(sizeof(localvar));
    // set val as empty if not provided
    if (cnt < nvars) {
        newvar->value = malloc(strlen("") + 1);
        strcpy(newvar->value, "");
    }
    else {
        newvar->value = malloc(strlen(tokens[1]) + 1);
        strcpy(newvar->value, tokens[1]);
    }
    newvar->name = malloc(strlen(tokens[0]) + 1);
    strcpy(newvar->name, tokens[0]);

    if (lhead == NULL) {
        newvar->idx = 0;
        newvar->next = NULL;
    }
    else {
        newvar->next = lhead;
        newvar->idx = lhead->idx+1;
    }
    lhead = newvar;

    free(dup_arg);
    freev((void*)tokens, nvars, 1);
    return 0;
}

// Usage: vars
int wsh_vars(size_t argc, char** args) {
    if (argc != 1 || args == NULL) return -1;

    localvar *i = lhead;
    while (i != NULL) {
        printf("%s=%s\n", i->name, i->value);
        i = i->next;
    }
    return 0;
}

// Usage: history
//        history <n>
//        history set <n>
int wsh_history(size_t argc, char** args) {
    if (argc > 3) return -1;

    // print history
    if (argc == 1 && args[0] != NULL) {
        hentry *curr = hhead;
        while (curr != NULL) {
            printf("%zu) ", (histentries - curr->idx));
            for (size_t i = 0; i < curr->argc; i++) {
                if (i == curr->argc-1)
                    printf("%s\n",(curr->argv)[i]);
                else
                    printf("%s ",(curr->argv)[i]);
            }
            curr = curr->next;
        }
    }
    // execute nth command
    else if (argc == 2 && args[1] != NULL) {
        errno = 0;
        char *endptr;
        long val = strtol(args[1], &endptr, 10);
        if (errno == ERANGE || *endptr != '\0') {
            fprintf(stderr, "history: n is not a valid numeric value\n");
            return -1;
        }

        if (val < 0) {
            fprintf(stderr, "history: n should be positive\n");
            return -1;
        }

        if ((size_t)val <= histentries && (size_t)val <= histsize) {
            hentry *curr = hhead;
            while (curr != NULL) {
                if ((size_t)val == (histentries - curr->idx)) {
                    char **parsed_tokens;
                    if ((parsed_tokens = parse_cmd(curr->argc, curr->argv)) == NULL) return -1;
                    return exec_cmd(curr->argc, parsed_tokens);
                }
                curr = curr->next;
            }
        }
    }
    // set history size
    else if (argc == 3 && args[1] != NULL && strcmp(args[1], "set") == 0 && args[2] != NULL) {
        errno = 0;
        char *endptr;
        long val = strtol(args[2], &endptr, 10);
        if (errno == ERANGE || *endptr != '\0') {
            fprintf(stderr, "history: n is not a valid numeric value\n");
            return -1;
        }

        if (val < 0) {
            fprintf(stderr, "history: n should be positive\n");
            return -1;
        }
        histsize = (size_t)val;
        // drop extra entries
        // TO-DO: logic can be moved into free_history()
        if (histsize < histentries) {
            size_t cnt = 0;
            hentry *curr = hhead;
            hentry *tmp = NULL;
            while (curr != NULL) {
                // remap index values according to new size
                // TO-DO: overflowing?
                curr->idx = histsize - cnt - 1;

                // free discarded entries
                if (cnt > histsize - 1) {
                    freev((void*)curr->argv, (curr->argc), 1);
                    tmp = curr->next;
                    curr->next = NULL;
                    free(curr);
                    curr = tmp;
                }

                // disconnect at new size
                if (cnt == histsize - 1) {
                    tmp = curr->next;
                    curr->next = NULL;
                    curr = tmp;
                }
                // TO-DO: causes SEGV on reducing history from 2 digit -> 1 digit sizes
                else { curr = curr->next; }
                cnt++;
            }
            histentries = histsize;
        }
    }
    else {
        fprintf(stderr, "Usage: %s | %s <n> | %s set <n>\n", args[0], args[0], args[0]);
        return -1;
    }
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
    while (i < n) {
        if (strncmp(pre, names[i]->d_name, strlen(pre)) != 0)
            printf("%s\n", names[i]->d_name);
        free(names[i]);
        i++;
    }
    free(names);
    return 0;
}
