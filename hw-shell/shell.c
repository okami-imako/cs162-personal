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
#include "helper.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

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

int resolve_process(char* token, char** full_path, char** file_name) {
  if (token[0] == '/') {
    *full_path = token;
    *file_name = extract_file_name(*full_path);
  } else {
    *file_name = token;
    *full_path = find_file_in_path(*file_name);
    if (!*full_path) {
      printf("cannot resolve %s\n", *file_name);
      return 0;
    }
  }
  return 1;
}

void run(struct tokens* tokens) {
  size_t tokens_len = tokens_get_length(tokens);
  if (tokens_len == 0) {
    return;
  }

  char* first_token = tokens_get_token(tokens, 0);
  char* full_path = NULL;
  char* file_name = NULL;
  if (!resolve_process(first_token, &full_path, &file_name)) {
    return;
  }

  char* args_buff[tokens_len];
  int ind;
  for (ind = 0; ind < tokens_len-1; ind++) {
    char* token = tokens_get_token(tokens, ind+1);
    if (is_keyword(token)) {
      break;
    }
    args_buff[ind] = token;
  }

  char** args = parse_args(args_buff, ind);
  args[0] = file_name;

  char* exec_conf_buff[tokens_len];
  for (int i = 0; i < tokens_len; i++) {
    exec_conf_buff[i] = NULL;
  }

  for (int i = 0; ind < tokens_len-1; ind++, i++) {
    char* token = tokens_get_token(tokens, ind+1);
    exec_conf_buff[i] = token;
  }

  exec_conf_t* exec_conf = parse_exec_conf(exec_conf_buff);

  fork_and_exec(full_path, args, exec_conf);
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
