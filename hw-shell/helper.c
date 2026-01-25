#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "helper.h"

#define PATH "PATH"

const char* const separator = ":";
const char* const keywords = "<>";

struct exec_conf {
  int stdin_fd;
  int stdout_fd;
};

exec_conf_t* init_exec_conf() {
  exec_conf_t* exec_conf = malloc(sizeof(exec_conf_t));
  exec_conf->stdin_fd = -1;
  exec_conf->stdout_fd = -1;
  return exec_conf;
}

typedef char** parser_t(char**, exec_conf_t*);

char** parser_in(char** tokens, exec_conf_t* conf);
char** parser_out(char** tokens, exec_conf_t* conf);

typedef struct parser_desc {
  char ch;
  parser_t* fun;
} parser_desc_t;

parser_desc_t parsers[] = {
  {'<', parser_in},
  {'>', parser_out},
};

parser_t* get_parser(char ch) {
  for (int i = 0; i < sizeof(parsers) / sizeof(parser_desc_t); i++) {
    if (parsers[i].ch == ch) {
      return parsers[i].fun;
    }
  }
  return NULL;
}

char** parser_in(char** tokens, exec_conf_t* exec_conf) {
  if (tokens[1] == NULL) {
    printf("expected filename after '<'\n");
    return NULL;
  }
  int fd = open(tokens[1], O_RDONLY);
  if (fd == -1) {
    printf("error opening %s\n", tokens[1]);
    perror("open");
    return NULL;
  }
  exec_conf->stdin_fd = fd;
  return &tokens[2];
}

char** parser_out(char** tokens, exec_conf_t* exec_conf) {
  if (tokens[1] == NULL) {
    printf("expected filename after '>'\n");
    return NULL;
  }
  int fd = open(tokens[1], O_WRONLY);
  if (fd == -1) {
    printf("error opening %s\n", tokens[1]);
    perror("open");
    return NULL;
  }
  exec_conf->stdout_fd = fd;
  return &tokens[2];
}

char* extract_file_name(char* path) {
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

char* join_path(char* first, char* second) {
  size_t size_first = strlen(first);
  size_t size_second = strlen(second);

  char* result = malloc((size_first + size_second + 2) * sizeof(char));

  strcpy(result, first);
  result[size_first] = '/';
  strcpy(result + size_first + 1, second);

  return result;
}

char* find_file_in_dir(char* dir_name, char* file_name) {
  DIR *dir = opendir(dir_name);
  if (dir == NULL) {
    return NULL;
  }

  char* full_path = NULL;
  for (struct dirent *dir_ent = readdir(dir); dir_ent != NULL; dir_ent = readdir(dir)) {
    if (strcmp(dir_ent->d_name, file_name) == 0) {
      full_path = join_path(dir_name, file_name);
      break;
    }
  }

  closedir(dir);
  return full_path;
}

char* find_file_in_path(char* file_name) {
  char* env_path = strdup(getenv(PATH));
  if (env_path == NULL) {
    return NULL;
  }

  char* full_path = NULL;
  char* saveptr;
  for (char* dir_name = strtok_r(env_path, separator, &saveptr);
       dir_name != NULL && full_path == NULL;
       dir_name = strtok_r(NULL, separator, &saveptr)) {
    full_path = find_file_in_dir(dir_name, file_name);
  }
  return full_path;
}

char** parse_args(char** args_buff, size_t buff_len) {
  char** args = malloc(sizeof(char*)*(buff_len+2));
  args[buff_len+1] = NULL;

  for (int i = 1; i < buff_len+1; i++) {
    args[i] = args_buff[i-1];
  }

  return args;
}

bool is_keyword(char* token) {
  return strlen(token) == 1 && strchr(keywords, token[0]) != NULL;
}

exec_conf_t* parse_exec_conf(char** exec_conf_buff) {
  exec_conf_t* exec_conf = init_exec_conf();

  char** ptr = exec_conf_buff;
  while (*ptr != NULL) {
    if (!is_keyword(*ptr)) {
      printf("don't know what '%s' means\n", *ptr);
      free(exec_conf);
      return NULL;
    }

    parser_t* parser = get_parser(**ptr);
    if (parser == NULL) {
      printf("unsupported character %c\n", **ptr);
      free(exec_conf);
      return NULL;
    }

    ptr = parser(ptr, exec_conf);
    if (ptr == NULL) {
      free(exec_conf);
      return NULL;
    }
  }

  return exec_conf;
}

void fork_and_exec(char* full_path, char* args[], exec_conf_t* exec_conf) {
  pid_t pid = fork();

  if (pid == -1) {
    printf("failed to fork\n");
    return;
  } else if (pid != 0) {
    int status;
    waitpid(pid, &status, 0);
  } else {
    if (exec_conf->stdin_fd != -1) {
      dup2(exec_conf->stdin_fd, STDIN_FILENO);
      close(exec_conf->stdin_fd);
    }
    if (exec_conf->stdout_fd != -1) {
      dup2(exec_conf->stdout_fd, STDOUT_FILENO);
      close(exec_conf->stdout_fd);
    }
    execv(full_path, args);
    printf("failed to exec %s\n", full_path);
  }
}

