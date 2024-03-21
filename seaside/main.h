#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <argp.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>

#define MAX_FILE_LEN 2048
#define DEFAULT_NODE_ID 0
#define BYTE_ALIGNMENT_64 64

#define MG_PRINT(format, args...)       \
do {                                    \
    printf(format, ##args);             \
} while (0);

#define MG_LOG(fd, format, args...)     \
do {                                    \
    fprintf(fd, format, ##args);        \
} while (0);

#define MG_LOG_PRINT(fd, format, args...)   \
do {                                        \
    MG_PRINT(format, ##args);               \
    MG_LOG(fd, format, ##args);             \
} while (0);

struct mg_options {
    char input_file[MAX_FILE_LEN];
    char dir[MAX_FILE_LEN];
    char log[MAX_FILE_LEN];
    bool use_dir;
    uint32_t threads;
    uint32_t min_cpr_lvl;
    uint32_t max_cpr_lvl;
    uint32_t obs;
    uint32_t obs_step;

    bool underflow;
    uint32_t ibc;
    uint32_t ibc_step;

    bool decomp_only;
    bool dynamic_only;
    bool static_only;
    bool debug;
    bool stateless;
    uint32_t zlibcompare;

    uint32_t processes;
};

size_t get_file_size(char *filename);
bool file_exists(char *filename);
int get_num_files(char *dirname);
int populate_file_list(char ***list, char *dirname, int start_idx);
