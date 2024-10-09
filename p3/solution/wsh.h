// built-in commands
#define EXIT    "exit"
#define CD      "cd"
#define EXPORT  "export"
#define LOCAL   "local"
#define VARS    "vars"
#define HISTORY "history"
#define LS      "ls"

int wsh_exit(size_t argc, char** args);
int wsh_cd(size_t argc, char** args);
int wsh_export(size_t argc, char** args);
int wsh_local(size_t argc, char** args);
int wsh_vars(size_t argc, char** args);
int wsh_history(size_t argc, char** args);
int wsh_ls(size_t argc, char** args);


// redirection tokens
#define R_IN      "<"
#define R_OUT     ">"
#define A_ROUT    "<<"
#define R_ERROUT  "&>"
#define A_RERROUT "&>>"

int r_input(size_t argc, char** args);
int r_output(size_t argc, char** args);
int a_routput(size_t argc, char** args);
int r_errout(size_t argc, char** args);
int a_rerrout(size_t argc, char** args);
