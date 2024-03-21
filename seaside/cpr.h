#pragma once

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include "main.h"
#include "cpa_sample_code_dc_utils.h"
#include "cpa_types.h"
#include "cpa.h"
#include "cpa_dc.h"
#include "qae_mem.h"
#include "icp_sal_user.h"

#define MAX_INSTANCES           (6)
#define DEFAULT_BUF_SIZE        (65536)
#define DEFAULT_DEST_BUF_SIZE   (4096)
#define NUM_INTER_BUFS          (2)
#define MAX_THREAD_COUNT        (1000)
#define MIN_OBS_VALUE           (128)
#define MIN_IBC_VALUE           (40)
#define DC_MAX_HISTORY_SIZE     (32768)
#define MAX_ALLOC_SIZE          (1024*1024)

#ifndef TAILQ_EMPTY
#define TAILQ_EMPTY(head)       ((head)->tqh_first == NULL)
#endif

extern CpaStatus qaeMemInit();
extern void qaeMemDestroy();

struct hw_setup_state {
    CpaInstanceHandle *inst_handle;
    CpaBufferList **inter_buf_list;
    uint16_t num_inst;
};

struct src_data {
    char filename[MAX_FILE_LEN];
    size_t file_size;
    size_t dcpr_size;
    Cpa8U *src_mem;
    Cpa32U ref_count;
    Cpa32U orig_ref_count;
    Cpa64U fail_count;
    pthread_mutex_t src_mutex;
};

struct sgl_container {
    uint32_t t_id;

    CpaBufferList *src_sgl;
    CpaBufferList *dest_sgl;
    CpaBufferList *context_sgl;
};

struct hw_setup_state g_hw_state;

CpaStatus cpr_start(struct mg_options *);
void *cpr_thread_entry(void *arg_id);
