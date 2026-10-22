#ifndef LAB_NODE_H
#define LAB_NODE_H

#include "ipc.h"

typedef struct {
    local_id id;
    int count;
    int rd[MAX_PROCESS_ID + 1];
    int wr[MAX_PROCESS_ID + 1];
} node_t;

#endif
