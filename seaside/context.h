#pragma once

#include <sys/queue.h>
#include "cpa.h"
#include "cpa_types.h"
#include "cpa_dc.h"
#include "main.h"
#include "cpr.h"

#define MAX_Q_SIZE      (32768)
#define DRAIN_Q_SIZE    (8192)

struct swresults {
    int status;
    uint32_t size;
    uint32_t crc32;
};	

struct context {
    Cpa64U id;
    Cpa32U nodeId;

    Cpa32U mem_size;
    Cpa8U *dest_mem;
    Cpa8U *compare_mem;
    Cpa8U *zlib_mem;

    CpaDcSessionHandle *sessCprHandle;
    CpaDcSessionSetupData sessCprSetupData;
    CpaDcSessionHandle *sessDcprHandle;
    CpaDcSessionSetupData sessDcprSetupData;


    struct src_data *src_data;

    Cpa32U obs;
    Cpa32U uf_ibc;

    CpaDcRqResults cpr_results;
    Cpa32U cpr_consumed;
    Cpa32U cpr_produced;

    CpaDcRqResults dcpr_results;
    Cpa32U dcpr_consumed;
    Cpa32U dcpr_produced;

    struct swresults zlib_results;


    bool decomp_only;
    bool underflow;
    bool debug;
    uint32_t zlibcompare;

    TAILQ_ENTRY(context) entries;
};

void ctx_init();
struct context *create_ctx(struct mg_options *opts, Cpa32U id);
void free_ctx(struct context *ctx, struct sgl_container *sgls);
void fill_ctx_sess(struct context *c, Cpa32U compLvl, Cpa32U huffType, Cpa32U sessState, Cpa32U deflateWindowSize);
CpaStatus launch_ctx(struct context *ctx, struct sgl_container *sgls);
uint32_t calculate_num_buf(uint32_t file_size, uint32_t buf_size);
void enq_ctx(struct context *ctx);
struct context *deq_ctx();

TAILQ_HEAD(, context) tailq_head;
