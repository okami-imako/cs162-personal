#pragma once

#include "tokenizer.h"

typedef struct exec_conf exec_conf_t;

exec_conf_t* parse_exec_conf(struct tokens* tokens);
void fork_and_exec(exec_conf_t* conf);
