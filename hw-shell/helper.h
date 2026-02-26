#pragma once

#include "tokenizer.h"

typedef struct exec_conf exec_conf_t;

exec_conf_t* build_exec_graph(struct tokens*);
void fork_and_exec(exec_conf_t* conf);
