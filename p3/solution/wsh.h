// built-in commands
#define EXIT    "exit"
#define CD      "cd"
#define EXPORT  "export"
#define LOCAL   "local"
#define VARS    "vars"
#define HISTORY "history"
#define LS      "ls"

int wsh_cd(size_t argc, char** args);
int wsh_export(size_t argc, char** args);
int wsh_local(size_t argc, char** args);
int wsh_vars(size_t argc, char** args);
int wsh_history(size_t argc, char** args);
int wsh_ls(size_t argc, char** args);


// redirection tokens
#define A_RERROUT "&>>"
#define R_ERROUT  "&>"
#define A_ROUT    ">>"
#define R_OUT     ">"
#define R_IN      "<"

int a_rerrout(char *filename, int fd);
int r_errout(char *filename, int fd);
int a_routput(char *filename, int fd);
int r_output(char *filename, int fd);
int r_input(char *filename, int fd);
