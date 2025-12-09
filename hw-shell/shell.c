#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))
#define PATH "PATH"

char* separator = ":";

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "cwd", "show current working directory"},
    {cmd_cd, "cd", "change current working directory"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

int cmd_pwd(unused struct tokens* tokens) {
  char* cwd = getcwd(NULL, 0);
  if (cwd == NULL) {
    return -1;
  }
  printf("%s\n", cwd);
  free(cwd);
  return 1;
}

int cmd_cd(struct tokens* tokens) {
  char* path = tokens_get_token(tokens, 1);
  return chdir(path);
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

char* extract_file_name(const char* path) {
  int i = 0;
  int last_delim_ind = 0;
  while (path[i] != '\0') {
    if (path[i] == '/') last_delim_ind = i;
    i++;
  }

  char* filename = (char*) malloc(sizeof(char) * (i - last_delim_ind));
  strcpy(filename, path+last_delim_ind+1);
  return filename;
}

char* join_path(char *first, char *second) {
  size_t size_first = strlen(first);
  size_t size_second = strlen(second);

  char* result = malloc((size_first + size_second + 2) * sizeof(char));

  strcpy(result, first);
  result[size_first] = '/';
  strcpy(result + size_first + 1, second);

  return result;
}

char* find_file_in_dir(char *dir_name, char *file_name) {
  DIR *dir = opendir(dir_name);
  if (dir == NULL) {
    printf("error opening dir %s\n", dir_name);
    return NULL;
  }

  for (struct dirent *dir_ent = readdir(dir); dir_ent != NULL; dir_ent = readdir(dir)) {
    printf("found %s\n", dir_ent->d_name);
    if (strcmp(dir_ent->d_name, file_name) == 0) {
      closedir(dir);
      return join_path(dir_name, file_name);
    }
  }

  closedir(dir);
  return NULL;
}

void run(struct tokens* tokens) {
  size_t tokens_len = tokens_get_length(tokens);
  if (tokens_len == 0) {
    return;
  }

  char *full_file_path, *file_name;
  char* first_token = tokens_get_token(tokens, 0);
  if (first_token[0] == '/') {
    full_file_path = first_token;
    file_name = extract_file_name(full_file_path);
  } else {
    file_name = first_token;

    char* env_path = strdup(getenv(PATH));
    if (env_path == NULL) {
      return;
    }

    char* saveptr;
    for (char* dir_name = strtok_r(env_path, separator, &saveptr);
         dir_name != NULL && full_file_path == NULL;
         dir_name = strtok_r(NULL, separator, &saveptr)) {
      full_file_path = find_file_in_dir(dir_name, file_name);
    }
  }

  char* args[tokens_len+1]; 
  args[0] = file_name;
  for (size_t i = 1; i < tokens_len; i++) {
    args[i] = tokens_get_token(tokens, i);
  }
  args[tokens_len] = NULL;

  pid_t pid = fork();

  if (pid == -1) {
    printf("failed to fork");
    return;
  } else if (pid != 0) {
    int status;
    waitpid(pid, &status, 0);
  } else {
    execv(full_file_path, args);
    printf("failed to exec %s\n", full_file_path);
  }
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      run(tokens);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
