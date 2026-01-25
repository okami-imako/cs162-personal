#pragma once

#include <stdio.h>

typedef struct exec_conf exec_conf_t;

char* extract_file_name(char* path);

char* join_path(char* first, char* second);

char* find_file_in_dir(char* dir_name, char* file_name);

char* find_file_in_path(char* file_name);

void fork_and_exec(char* full_path, char* args[], exec_conf_t* exec_conf);

char** parse_args(char** args_buff, size_t buff_len);

bool is_keyword(char* token);

exec_conf_t* parse_exec_conf(char** exec_conf_buff);
