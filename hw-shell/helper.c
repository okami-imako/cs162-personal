#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "helper.h"
#include "util.h"

const char* const keywords = "<>";

bool is_keyword(char* token) {
  return strlen(token) == 1 && strchr(keywords, token[0]) != NULL;
}

struct exec_conf {
  char* full_path;
  char* file_name;
  char** args;
  int stdin_fd;
  int stdout_fd;
};

exec_conf_t* init_exec_conf() {
  exec_conf_t* exec_conf = malloc(sizeof(exec_conf_t));
  exec_conf->stdin_fd = -1;
  exec_conf->stdout_fd = -1;
  return exec_conf;
}

void free_conf(exec_conf_t* conf) {
  if (conf->full_path) {
    free(conf->full_path);
  }
  if (conf->file_name) {
    free(conf->file_name);
  }
  free(conf);
}

typedef size_t parser_t(size_t, struct tokens*, exec_conf_t*);

size_t parse_executable(size_t, struct tokens*, exec_conf_t*);
size_t parse_args(size_t, struct tokens*, exec_conf_t*);
size_t parse_in(size_t, struct tokens*, exec_conf_t*);
size_t parse_out(size_t, struct tokens*, exec_conf_t*);

typedef struct parser_desc {
  char* keyword;
  parser_t* fun;
} parser_desc_t;

parser_desc_t parsers[] = {
  {"<", parse_in},
  {">", parse_out},
};

parser_t* get_parser(char* token) {
  for (int i = 0; i < sizeof(parsers) / sizeof(parser_desc_t); i++) {
    if (strcmp(parsers[i].keyword, token) == 0) {
      return parsers[i].fun;
    }
  }
  return NULL;
}

exec_conf_t* parse_exec_conf(struct tokens* tokens) {
  exec_conf_t* exec_conf = init_exec_conf();

  size_t ind = 0;
  while (ind < tokens_get_length(tokens)) {
    ind = parse_executable(ind, tokens, exec_conf);
    if (ind == -1) {
      free_conf(exec_conf);
      return NULL;
    }

    ind = parse_args(ind, tokens, exec_conf);

    while (ind < tokens_get_length(tokens)) {
      char* keyword = tokens_get_token(tokens, ind);
      parser_t* parser = get_parser(keyword);
      if (parser == NULL) {
        printf("unsupported keyword %s\n", keyword);
        free_conf(exec_conf);
        return NULL;
      }

      ind = parser(ind, tokens, exec_conf);
      if (ind == -1) {
        free_conf(exec_conf);
        return NULL;
      }
    }
  }

  return exec_conf;
}

void fork_and_exec(exec_conf_t* conf) {
  pid_t pid = fork();

  if (pid == -1) {
    printf("failed to fork\n");
    return;
  } else if (pid != 0) {
    int status;
    waitpid(pid, &status, 0);
  } else {
    if (conf->stdin_fd != -1) {
      dup2(conf->stdin_fd, STDIN_FILENO);
      close(conf->stdin_fd);
    }
    if (conf->stdout_fd != -1) {
      dup2(conf->stdout_fd, STDOUT_FILENO);
      close(conf->stdout_fd);
    }
    execv(conf->full_path, conf->args);
    printf("failed to exec %s\n", conf->full_path);
  }
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

size_t parse_in(size_t ind, struct tokens* tokens, exec_conf_t* exec_conf) {
  if (ind == tokens_get_length(tokens)-1) {
    printf("expected filename after '<'\n");
    return -1;
  }
  char* file_name = tokens_get_token(tokens, ind+1);
  int fd = open(file_name, O_RDONLY);
  if (fd == -1) {
    printf("error opening %s\n", file_name);
    perror("open");
    return -1;
  }
  exec_conf->stdin_fd = fd;
  return ind+2;
}

size_t parse_out(size_t ind, struct tokens* tokens, exec_conf_t* exec_conf) {
  if (ind == tokens_get_length(tokens)-1) {
    printf("expected filename after '>'\n");
    return -1;
  }
  char* file_name = tokens_get_token(tokens, ind+1);
  int fd = open(file_name, O_WRONLY);
  if (fd == -1) {
    printf("error opening %s\n", file_name);
    perror("open");
    return -1;
  }
  exec_conf->stdout_fd = fd;
  return ind+2;
}

