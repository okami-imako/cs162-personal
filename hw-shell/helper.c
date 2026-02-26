#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "helper.h"
#include "tokenizer.h"
#include "util.h"

const char* const special_symbols = "<>";
const char* const flow_control_symbols = "|";

bool is_special_symbol(char* token) {
  return strlen(token) == 1 && strchr(special_symbols, token[0]) != NULL;
}

bool is_flow_control_symbol(char* token) {
  return strlen(token) == 1 && strchr(flow_control_symbols, token[0]) != NULL;
}

bool is_keyword(char* token) {
  return is_special_symbol(token) || is_flow_control_symbol(token);
}

struct exec_conf {
  char* full_path;
  char* file_name;
  char** args;
  int stdin_fd;
  int stdout_fd;
  int fd_to_close[2];
  struct exec_conf* next;
};

exec_conf_t* init_exec_conf() {
  exec_conf_t* exec_conf = malloc(sizeof(exec_conf_t));
  *exec_conf = (exec_conf_t){ NULL, NULL, NULL, -1, -1, {-1, -1}, NULL };
  return exec_conf;
}

void close_io_fds(exec_conf_t* conf) {
  if (conf->stdin_fd != -1) {
    close(conf->stdin_fd);
  }
  if (conf->stdout_fd != -1) {
    close(conf->stdout_fd);
  }
}

void close_unused_fds(exec_conf_t* conf) {
  if (conf->fd_to_close[0] != -1) close(conf->fd_to_close[0]);
  if (conf->fd_to_close[1] != -1) close(conf->fd_to_close[1]);
}

void free_conf(exec_conf_t* conf) {
  if (conf->full_path) {
    free(conf->full_path);
  }
  if (conf->file_name) {
    free(conf->file_name);
  }
  close_io_fds(conf);
  close_unused_fds(conf);
  if (conf->next != NULL) {
    free_conf(conf->next);
  }
  free(conf);
}

typedef size_t special_symbol_parser_t(size_t, struct tokens*, exec_conf_t*);

size_t parse_exec_conf(size_t, struct tokens*, exec_conf_t*);
size_t parse_executable(size_t, struct tokens*, exec_conf_t*);
size_t parse_args(size_t, struct tokens*, exec_conf_t*);
size_t parse_special_symblos(size_t, struct tokens*, exec_conf_t*);
size_t parse_in(size_t, struct tokens*, exec_conf_t*);
size_t parse_out(size_t, struct tokens*, exec_conf_t*);

typedef struct special_symbol_parser {
  char* keyword;
  special_symbol_parser_t* fun;
} special_symbol_parser_desc_t;

special_symbol_parser_desc_t special_symbol_parsers[] = {
  {"<", parse_in},
  {">", parse_out},
};

special_symbol_parser_t* get_parser(char* token) {
  for (int i = 0; i < sizeof(special_symbol_parsers) / sizeof(special_symbol_parser_desc_t); i++) {
    if (strcmp(special_symbol_parsers[i].keyword, token) == 0) {
      return special_symbol_parsers[i].fun;
    }
  }
  return NULL;
}

typedef exec_conf_t* flow_control_parser_t(exec_conf_t*);

exec_conf_t* parse_pipe(exec_conf_t*);

typedef struct flow_control_parser_desc {
  char* keyword;
  flow_control_parser_t* fun;
} flow_control_parser_desc_t;

flow_control_parser_desc_t flow_control_parsers[] = {
  {"|", parse_pipe},
};

flow_control_parser_t* get_flow_control_parser(char* token) {
  for (int i = 0; i < sizeof(flow_control_parsers) / sizeof(flow_control_parser_desc_t); i++) {
    if (strcmp(flow_control_parsers[i].keyword, token) == 0) {
      return flow_control_parsers[i].fun;
    }
  }
  return NULL;
}

exec_conf_t* build_exec_graph(struct tokens* tokens) {
  exec_conf_t* root = init_exec_conf();
  exec_conf_t* curr_conf = root;
  for (size_t ind = 0; ind < tokens_get_length(tokens); ind++) {
    ind = parse_exec_conf(ind, tokens, curr_conf);
    if (ind == -1) {
      free_conf(root);
      return NULL;
    }

    if (ind >= tokens_get_length(tokens)) {
      return root;
    }

    char* token = tokens_get_token(tokens, ind);
    flow_control_parser_t* parser = get_flow_control_parser(token);
    if (parser == NULL) {
      printf("unsupported flow control symbol %s\n", token);
      free_conf(root);
      return NULL;
    }

    curr_conf = parser(curr_conf);
    if (curr_conf == NULL) {
      free_conf(root);
      return NULL;
    }
  }

  return root;
}

void fork_and_exec(exec_conf_t* conf) {
  unsigned int task_count = 0;
  for (exec_conf_t* curr = conf; curr != NULL; curr = curr->next) {
    task_count++;
  }

  int pids[task_count];

  int i = 0;
  for (exec_conf_t* curr = conf; curr != NULL; curr = curr->next, i++) {
    pid_t pid = fork();

    if (pid == -1) {
      printf("failed to fork\n");
      free_conf(conf);
      break;
    } else if (pid != 0) {
      pids[i] = pid;
      close_io_fds(curr);
    } else {
      if (curr->stdin_fd != -1) {
        dup2(curr->stdin_fd, STDIN_FILENO);
        close(curr->stdin_fd);
      }
      if (curr->stdout_fd != -1) {
        dup2(curr->stdout_fd, STDOUT_FILENO);
        close(curr->stdout_fd);
      }
      close_unused_fds(conf);
      execv(curr->full_path, curr->args);
      printf("failed to exec %s\n", curr->full_path);
      exit(1);
    }
  }

  for (int i = 0; i < task_count; i++) {
    int status;
    waitpid(pids[i], &status, 0);
  }

  free_conf(conf);
}

size_t parse_exec_conf(size_t ind, struct tokens* tokens, exec_conf_t* conf) {
  ind = parse_executable(ind, tokens, conf);
  if (ind == -1) {
    return -1;
  }

  ind = parse_args(ind, tokens, conf);
  if (ind == -1) {
    return -1;
  }

  ind = parse_special_symblos(ind, tokens, conf);
  if (ind == -1) {
    return -1;
  }

  return ind;
}

size_t parse_executable(size_t ind, struct tokens* tokens, exec_conf_t* conf) {
  char* token = tokens_get_token(tokens, ind);
  if (token[0] == '/') {
    conf->full_path = copy_str(token, strlen(token));
    conf->file_name = extract_file_name(token);
  } else {
    conf->full_path = locate_file(token);
    if (!conf->full_path) {
      printf("cannot resolve %s\n", token);
      return -1;
    }
    conf->file_name = copy_str(token, strlen(token));
  }
  return ind+1;
}

size_t parse_args(size_t ind, struct tokens* tokens, exec_conf_t* conf) {
  size_t args_len = 0;
  char** args_buff = calloc(tokens_get_length(tokens)-ind, sizeof(char*));

  for (; ind < tokens_get_length(tokens); ind++) {
    char* token = tokens_get_token(tokens, ind);
    if (is_keyword(token)) {
      break;
    }
    args_buff[args_len++] = token;
  }

  conf->args = calloc(args_len+2, sizeof(char*));
  conf->args[0] = conf->file_name;
  conf->args[args_len+1] = NULL;
  for (int i = 0; i < args_len; i++) {
    conf->args[i+1] = args_buff[i];
  }

  free(args_buff);
  return ind;
}

size_t parse_special_symblos(size_t ind, struct tokens* tokens, exec_conf_t* conf) {
  while (ind < tokens_get_length(tokens)) {
    char* keyword = tokens_get_token(tokens, ind);
    if (!is_special_symbol(keyword)) {
      return ind;
    }

    special_symbol_parser_t* parser = get_parser(keyword);
    if (parser == NULL) {
      printf("unsupported special symbol %s\n", keyword);
      return -1;
    }

    ind = parser(ind, tokens, conf);
    if (ind == -1) {
      return -1;
    }
  }

  return ind;
}

size_t parse_in(size_t ind, struct tokens* tokens, exec_conf_t* conf) {
  if (ind == tokens_get_length(tokens)-1) {
    printf("expected filename after '<'\n");
    return -1;
  }
  if (conf->stdin_fd != -1) {
    printf("%s already has redirected input\n", conf->full_path);
    return -1;
  }

  char* file_name = tokens_get_token(tokens, ind+1);
  int fd = open(file_name, O_RDONLY);
  if (fd == -1) {
    printf("error opening %s\n", file_name);
    perror("open");
    return -1;
  }
  conf->stdin_fd = fd;
  return ind+2;
}

size_t parse_out(size_t ind, struct tokens* tokens, exec_conf_t* conf) {
  if (ind == tokens_get_length(tokens)-1) {
    printf("expected filename after '>'\n");
    return -1;
  }
  if (conf->stdout_fd != -1) {
    printf("%s already has redirected output\n", conf->full_path);
    return -1;
  }

  char* file_name = tokens_get_token(tokens, ind+1);
  int fd = open(file_name, O_WRONLY | O_CREAT);
  if (fd == -1) {
    printf("error opening %s\n", file_name);
    perror("open");
    return -1;
  }
  conf->stdout_fd = fd;
  return ind+2;
}

exec_conf_t* parse_pipe(exec_conf_t* prev) {
  if (prev->stdout_fd != -1) {
    printf("%s already has redirected output\n", prev->full_path);
    return NULL;
  }

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    perror("pipe");
    return NULL;
  }

  exec_conf_t* next = init_exec_conf();
  prev->next = next;

  next->stdin_fd = pipefd[0];
  prev->stdout_fd = pipefd[1];

  next->fd_to_close[1] = pipefd[1];
  prev->fd_to_close[0] = pipefd[0];

  return next;
}
