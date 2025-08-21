#ifndef GAME_FUNCTIONS_H
#define GAME_FUNCTIONS_H

#include <stdio.h>
#include <stddef.h>

int create_shared_memory(const char* name, size_t size);
int destroy_shared_memory(const char* name);

#endif