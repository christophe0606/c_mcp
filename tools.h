#ifndef tools_h
#define tools_h

#include "cJSON.h"

extern void define_tools();
extern cJSON *handle_tools_call(cJSON *id, cJSON *params);

#endif