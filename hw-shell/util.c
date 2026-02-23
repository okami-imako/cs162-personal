#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define PATH "PATH"

const char* const separator = ":";


char* find_file_in_dir(char* dir_name, char* file_name);

char* locate_file(char* file_name) {
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

char* copy_str(char* src, size_t n) {
  src[n] = '\0';
  char* dst = malloc(n+1);
  strcpy(dst, src);
  return dst;
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
