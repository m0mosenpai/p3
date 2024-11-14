#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/limits.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
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

static char *redirects[TOTAL_REDIRECTIONS] = {
    A_RERROUT,
    R_ERROUT,
    A_ROUT,
    R_OUT,
    R_IN,
};
static int (*redirect_fn[TOTAL_REDIRECTIONS])(char*, int) = {
    &a_rerrout,
    &r_errout,
    &a_routput,
    &r_output,
    &r_input,
};

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

void free_history(hentry *i) {
    hentry *tmp = NULL;
    while (i != NULL) {
        freev((void*)i->argv, (i->argc), 1);
        tmp = i->next;
        free(i);
        i = tmp;
    }
}

void log_in_history(size_t argc, char **argv) {
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
                if (hhead->argv[i] == NULL || argv[i] == NULL) {
                    is_repeated = 0;
                    break;
                }
                if (strcmp(hhead->argv[i], argv[i]) != 0) {
                    is_repeated = 0;
                    break;
                }
            }
        }
        else { is_repeated = 0; }

        if (!is_repeated) {
            hentry *newentry = malloc(sizeof(hentry));
            newentry->argc = argc;
            newentry->argv = malloc(argc * sizeof(char*));
            for (size_t i = 0; i < argc; i++) {
                newentry->argv[i] = malloc(strlen(argv[i]) + 1);
                strcpy(newentry->argv[i], argv[i]);
            }
            newentry->next = hhead;
            newentry->idx = hhead->idx + 1;
            hhead = newentry;

            // drop last element if max size reached
            if (histentries == histsize) {
                hentry *curr = hhead;
                hentry *tmp = NULL;
                size_t cnt = 0;
                while (curr != NULL) {
                    curr->idx = histentries - cnt - 1;
                    if (cnt == histsize - 1) {
                        tmp = curr->next;
                        curr->next = NULL;
                        curr = tmp;
                        free_history(curr);
                        break;
                    }
                    curr = curr->next;
                    cnt++;
                }
            } else {
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
    char pre[2] = "$";
    if (strncmp(pre, token, strlen(pre)) == 0) {
        char *tok_dup = strdup(token);
        char *tok = strtok(tok_dup, pre);

        // 1. check env
        char *val;
        if ((val = getenv(tok)) != NULL) {
            // TO-DO: extra free byte bug?
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

char **parse_cmd(size_t argc, char **argv) {
    char *var_v = NULL;
    char **argv_parsed = calloc(argc + 1, sizeof(char*));
    size_t i = 0;
    for (i = 0; i < argc; i++) {
        // check for whole variables strings
        if ((var_v = fetch_if_var(argv[i])) != NULL) {
            argv_parsed[i] = malloc(strlen(var_v) + 1);
            strcpy(argv_parsed[i], var_v);
            continue;
        }

        // check for variables in key/value pairs
        char *ptr = NULL;
        if ((ptr = strchr(argv[i], '=')) != NULL) {
            char delim[2] = "=";
            size_t cnt = 0;
            const size_t nvars = 2;
            size_t ttok_len = 0;
            char *arg_dup = strdup(argv[i]);
            char *token = strtok(arg_dup, delim);
            char **tokens = calloc(nvars, sizeof(char*));
            while (token != NULL) {
                // ignore any extra tokens
                if (cnt > nvars-1) break;

                ptr = NULL;
                var_v = NULL;
                if ((ptr = strchr(token, '$')) != NULL) {
                    if (cnt == 0) {
                        free(arg_dup);
                        freev((void*)argv_parsed, argc, 1);
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
            free(arg_dup);


            // 2 extra bytes -> "=" and null byte
            argv_parsed[i] = malloc(ttok_len + 2);
            snprintf(argv_parsed[i], ttok_len + 2, "%s=%s", tokens[0], tokens[1]);
            freev((void*)tokens, nvars, 1);
            continue;
        }

        argv_parsed[i] = malloc(strlen(argv[i]) + 1);
        strcpy(argv_parsed[i], argv[i]);
    }
    argv_parsed[i] = NULL;
    return argv_parsed;
}

int exec_cmd(size_t argc, char **argv) {
    if (argc == 0 || argv == NULL) return -1;

    // 1. check if cmd is built-in
    for (size_t i = 0; i < TOTAL_BUILTINS; i++) {
        if (strcmp(argv[0], builtins[i]) == 0) {
            int rc = ((*builtin_fn[i])(argc, argv));
            return rc;
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

    return -1;
}

// generate prompt
int prompt(FILE *stream) {
    int rc = 1;
    if (stream == stdin) {
        rc = printf("wsh> ");
        fflush(stdout);
    }
    return rc;
}

int main(int argc, char *argv[]) {
    // intialize default PATH
    if (setenv(PATH, DEFAULT_PATH, 1) != 0) {
        shell_rc = -1;
        return shell_rc;
    }

    // intialize file descriptor values
    int old_stdout = 0;
    int old_stdin = 0;
    int old_stderr = 0;
    FILE *instream = NULL;

    if (argc == 1) instream = stdin;
    else if (argc == 2) {
        const char *scriptFile = argv[1];
        instream = fopen(scriptFile, "r");
        if (instream == NULL) {
            shell_rc = -1;
            return shell_rc;
        }
    }
    else {
        shell_rc = -1;
        return shell_rc;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    size_t ntoks = MIN_TOKEN_LIST_SIZE;
    size_t cnt = 0;
    char delim[2] = " ";
    int allspace = 1;
    while (prompt(instream) && (read = getline(&line, &len, instream)) != -1) {
        // ignore blank lines
        allspace = 1;
        if (strcmp("\n", line) == 0) continue;
        for (size_t i = 0; i < strlen(line); i++) if (!isspace(line[i])) { allspace = 0; break; }
        if (allspace == 1) continue;

        // ignore comments
        char com[2] = "#";
        if (strncmp(line, com, strlen(com)) == 0) continue;

        cnt = 0;
        ntoks = MIN_TOKEN_LIST_SIZE;
        char **tokens = calloc(ntoks, sizeof(char*));
        char *token = strtok(line, delim);
        while (token != NULL) {
            // strip newline char
            char *ptr = NULL;
            if ((ptr = strchr(token, '\n'))) *ptr = '\0';

            if (cnt >= ntoks) {
                ntoks *= 2;
                tokens = reallocarray(tokens, ntoks, sizeof(char*));
            }
            tokens[cnt] = malloc(strlen(token) + 1);
            strcpy(tokens[cnt], token);
            cnt++;
            token = strtok(NULL, delim);
        }


        // 1. log in history (excluding builtins)
        int isbuiltin = 0;
        for (size_t i = 0; i < TOTAL_BUILTINS; i++)
            if (strcmp(tokens[0], builtins[i]) == 0) { isbuiltin = 1; break; }
        if (!isbuiltin) log_in_history(cnt, tokens);

        // 2. handle redirections
        int redir_rc = 1;
        for (size_t r = 0; r < TOTAL_REDIRECTIONS; r++) {
            char *redir = NULL;
            char *dup_arg = strdup(tokens[cnt - 1]);
            if ((redir = strstr(dup_arg, redirects[r])) != NULL) {
                // check if it precedes with a fd num
                char *ptr = tokens[cnt - 1];
                int fd = -1;
                while (isdigit(*ptr) != 0) {
                    if (fd == -1) fd = 0;
                    fd = fd * 10 + (*ptr - '0');
                    ptr++;
                }

                *redir = '\0';
                redir += strlen(redirects[r]);
                if (redir != NULL) {
                    old_stdout = dup(STDOUT_FILENO);
                    old_stdin = dup(STDIN_FILENO);
                    old_stderr = dup(STDERR_FILENO);
                    if ((redir_rc = (*redirect_fn[r])(redir, fd)) == -1)
                        shell_rc = redir_rc;
                    free(dup_arg);
                    break;
                }
            }
            free(dup_arg);
        }

        if (redir_rc >= 0) {
            if (redir_rc == 0) cnt--;

            // exit gracefully if user inputs "exit"
            if (strcmp(tokens[0], EXIT) == 0) {
                if (cnt != 1) { shell_rc = -1; continue; }
                else {
                    close(old_stdout);
                    close(old_stdin);
                    close(old_stderr);
                    fclose(instream);
                    freev((void*)tokens, ntoks, 1);
                    free(line);
                    free_locals();
                    free_history(hhead);
                    exit(shell_rc);
                }
            }

            // 3. parse vars
            char **parsed_tokens;
            if ((parsed_tokens = parse_cmd(cnt, tokens)) == NULL) {
                shell_rc = -1;
            }

            // 3. execute
            else {
                shell_rc = exec_cmd(cnt, parsed_tokens);
                dup2(STDOUT_FILENO, old_stdout);
                dup2(STDIN_FILENO, old_stdin);
                dup2(STDERR_FILENO, old_stderr);
                freev((void*)parsed_tokens, cnt, 1);
            }
        }
        freev((void*)tokens, ntoks, 1);
    }
    close(old_stdout);
    close(old_stdin);
    close(old_stderr);
    fclose(instream);
    free(line);
    free_locals();
    free_history(hhead);
    return shell_rc;
}


// --------REDIRECTIONS---------
//
// &>>
int a_rerrout(char *fname, int fd) {
    if (fname == NULL || fd != -1) return -1;

    int file;
    file = open(fname, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (file == -1) return -1;

    dup2(file, STDOUT_FILENO);
    dup2(file, STDERR_FILENO);

    close(file);
    return 0;
}

// &>
int r_errout(char *fname, int fd) {
    if (fname == NULL || fd != -1) return -1;

    int file;
    file = open(fname, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (file == -1) return -1;

    dup2(file, STDOUT_FILENO);
    dup2(file, STDERR_FILENO);

    close(file);
    return 0;
}

// >>
int a_routput(char *fname, int fd) {
    if (fname == NULL) return -1;

    int file;
    file = open(fname, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (file == -1) return -1;

    if (fd == -1) dup2(file, STDOUT_FILENO);
    else dup2(file, fd);

    close(file);
    return 0;
}

// >
int r_output(char *fname, int fd) {
    if (fname == NULL) return -1;

    int file;
    file = open(fname, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (file == -1) return -1;

    if (fd == -1) dup2(file, STDOUT_FILENO);
    else dup2(file, fd);

    close(file);
    return 0;
}

// <
int r_input(char *fname, int fd) {
    if (fname == NULL) return -1;

    int file;
    file = open(fname, O_RDONLY, S_IRUSR);
    if (file == -1) return -1;

    if (fd == -1) dup2(file, STDIN_FILENO);
    else dup2(file, fd);

    close(file);
    return 0;
}


// --------BUILT-IN COMMANDS---------
//
// Usage: cd <dir-path>
int wsh_cd(size_t argc, char** args) {
    if (argc != 2) return -1;
    char *var_v = fetch_if_var(args[1]);
    if (chdir(var_v == NULL ? args[1] : var_v) != 0) return -1;
    return 0;
}

// Usage: export <name>=<val>
//        export <name>=$VAR
int wsh_export(size_t argc, char** args) {
    if (argc != 2) return -1;

    size_t cnt = 0;
    const size_t nvars = 2;
    char delim[2] = "=";
    char *dup_arg = strdup(args[1]);
    char *token = strtok(dup_arg, delim);
    char **tokens = calloc(nvars, sizeof(char*));
    while (token != NULL) {
        tokens[cnt] = malloc(strlen(token) + 1);
        strcpy(tokens[cnt++], token);
        token = strtok(NULL, delim);
    }

    // should have atleast 2 tokens for key-val pair
    if (cnt < nvars) {
        free(dup_arg);
        freev((void*)tokens, nvars, 1);
        return -1;
    }

    int rc = 0;
    if ((rc = setenv(tokens[0], tokens[1], 1)) != 0) rc = -1;
    free(dup_arg);
    freev((void*)tokens, nvars, 1);
    return rc;
}

// Usage: local <name>=<val>
//        local <name>=$VAR
int wsh_local(size_t argc, char** args) {
    if (argc != 2 || args == NULL) return -1;

    size_t cnt = 0;
    const size_t nvars = 2;
    char delim[2] = "=";
    char *dup_arg = strdup(args[1]);
    char *token = strtok(dup_arg, delim);
    char **tokens = calloc(nvars, sizeof(char*));
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
        lhead = newvar;
    }
    else {
        lhead->next = newvar;
        newvar->idx = lhead->idx+1;
    }
    newvar->next = NULL;

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
    if (argc == 1 && args[0] != NULL && histentries > 0) {
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
        if (errno == ERANGE || *endptr != '\0' || val < 0) return -1;

        if ((size_t)val <= histentries && (size_t)val <= histsize) {
            hentry *curr = hhead;
            while (curr != NULL) {
                if ((size_t)val == (histentries - curr->idx)) {
                    char **parsed_tokens;
                    if ((parsed_tokens = parse_cmd(curr->argc, curr->argv)) == NULL) return -1;
                    int rc = exec_cmd(curr->argc, parsed_tokens);
                    freev((void*)parsed_tokens, curr->argc, 1);
                    return rc;
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
        if (errno == ERANGE || *endptr != '\0' || val < 0) return -1;
        histsize = (size_t)val;
        if (histsize == 0) return -1;

        // drop extra entries
        if (histsize < histentries) {
            size_t cnt = 0;
            hentry *curr = hhead;
            hentry *tmp = NULL;
            while (curr != NULL) {
                // remap index values according to new size
                curr->idx = histsize - cnt - 1;

                // disconnect and free discarded entries
                if (cnt == histsize - 1) {
                    tmp = curr->next;
                    curr->next = NULL;
                    curr = tmp;
                    free_history(curr);
                    break;
                }
                else { curr = curr->next; }
                cnt++;
            }
            histentries = histsize;
        }
    }
    else { return -1;}
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
    while (i < n) {
        if (strncmp(pre, names[i]->d_name, strlen(pre)) != 0)
            printf("%s\n", names[i]->d_name);
        free(names[i]);
        i++;
    }
    free(names);
    return 0;
}
