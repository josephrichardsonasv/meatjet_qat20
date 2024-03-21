#include "cpr.h"
#include "main.h"
#include "context.h"
#include "buf_handler.h"

#ifdef MG_UNIT_TEST
#include "mg_unit_test.h"
#endif

#ifdef DEBUG_CODE
extern Cpa32U g_alloc;
extern Cpa32U g_free;
extern pthread_mutex_t mem_mutex;
#endif

extern CpaInstanceHandle *dcInstances_g;
extern Cpa16U numDcInstances_g;
extern FILE *g_log_fd;

uint64_t g_q_size;
bool g_draining_q;
pthread_mutex_t queue_mutex;
pthread_mutex_t queue_size_mutex;

/*
    Function:

        ctx_init

    Description:

        initializes the Q mutex, and Q itself

    Parameters:

        none

    Return:

        none
*/
void ctx_init()
{
    if (pthread_mutex_init(&queue_mutex, NULL)) {
        MG_LOG_PRINT(g_log_fd, "Failed to init queue mutex\n");
    }

    if (pthread_mutex_init(&queue_size_mutex, NULL)) {
        MG_LOG_PRINT(g_log_fd, "Failed to init queue size mutex\n");
    }

    g_q_size = 0;
    g_draining_q = false;

    TAILQ_INIT(&tailq_head);
}

/*
    Function:

        create_ctx

    Description:

        Allocates and initializes a new context

    Parameters:

        opts    -   Ptr to command line options struct
        id      -   Context ID number

    Return:

        Ptr to the newly alloc'd context
*/
struct context *create_ctx(struct mg_options *opts, Cpa32U id)
{
    struct context *ctx;
    //CpaInstanceInfo2 pInstanceInfo2;

    ctx = (struct context*)calloc(1, sizeof(struct context));

    if (opts == NULL)
    {
        MG_LOG_PRINT(g_log_fd, "Error: opts cannot be NULL\n");
        free(ctx);
        return NULL;
    }

    ctx->id = id;

    /*status = cpaDcInstanceGetInfo2(dcInstances_g[id%numDcInstances_g], &pInstanceInfo2);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: Could not get instance information\n");
        exit(status);
    }
    ctx->nodeId = pInstanceInfo2.nodeAffinity;
    */
    ctx->nodeId = 0;
    ctx->decomp_only = opts->decomp_only;
    ctx->underflow = opts->underflow;
    ctx->debug = opts->debug;
    ctx->zlibcompare = opts->zlibcompare;

    return ctx;
}

/*
    Function:

        free_ctx

    Description:

        Frees all related context mem -- sessions and mem buffers

    Parameters:

        ctx     -   Ptr to the context
        sgls    -   Ptr to the sgl container to determine instance

    Return:

        none
*/
void free_ctx(struct context *ctx, struct sgl_container *sgls)
{
    uint32_t iNum;

    iNum = sgls->t_id % numDcInstances_g;

    cpaDcRemoveSession(dcInstances_g[iNum], ctx->sessCprHandle);
    qaeMemFreeNUMA((void**)&(ctx->sessCprHandle));
    cpaDcRemoveSession(dcInstances_g[iNum], ctx->sessDcprHandle);
    qaeMemFreeNUMA((void**)&(ctx->sessDcprHandle));

#ifdef DEBUG_CODE
    pthread_mutex_lock(&mem_mutex);
    g_free++;
    pthread_mutex_unlock(&mem_mutex);
#endif

    free(ctx->dest_mem);
    free(ctx->compare_mem);
    if(ctx->zlibcompare)
	    free(ctx->zlib_mem);

    free(ctx);
}

/*
    Function:

        fill_ctx_sess

    Description:

        Copies necessary cfg info into the ctx session structure

    Parameters:

        c                   -   Ptr to the context
        compLvl             -   Compression level 1-9
        huffType            -   Huffman encoding type Static vs Dynamic
        sessState           -   Stateful or Stateless
        deflateWindowSize   -   Window size

    Return:

        none
*/
void fill_ctx_sess(struct context *c, Cpa32U compLvl, Cpa32U huffType, Cpa32U sessState, Cpa32U deflateWindowSize)
{
    // Populate the session data
    c->sessCprSetupData.compLevel = compLvl;     // 1-9 mimicking gzip
    c->sessCprSetupData.huffType = huffType;     // CPA_DC_HT_STATIC/FULL_DYNAMIC
    c->sessCprSetupData.sessState = sessState;   // CPA_DC_STATELESS/STATEFUL
    c->sessCprSetupData.windowSize = deflateWindowSize;

    // Values that won't change for this application
    if (CPA_DC_STATELESS == sessState) {
        c->sessCprSetupData.sessDirection = CPA_DC_DIR_COMPRESS;
    }
    else {
        c->sessCprSetupData.sessDirection = CPA_DC_DIR_COMBINED;
    }
    c->sessCprSetupData.compType = CPA_DC_DEFLATE;
    //c->sessCprSetupData.fileType = CPA_DC_FT_OTHER;
    c->sessCprSetupData.autoSelectBestHuffmanTree = CPA_DC_ASB_STATIC_DYNAMIC;
    c->sessCprSetupData.checksum = CPA_DC_CRC32;

    // Populate the session data
    c->sessDcprSetupData.compLevel = compLvl;     // 1-9 mimicking gzip
    c->sessDcprSetupData.huffType = huffType;     // CPA_DC_HT_STATIC/FULL_DYNAMIC
    c->sessDcprSetupData.sessState = CPA_DC_STATEFUL;   // CPA_DC_STATELESS/STATEFUL
    c->sessDcprSetupData.windowSize = deflateWindowSize;

    // Values that won't change for this application
    c->sessDcprSetupData.sessDirection = CPA_DC_DIR_DECOMPRESS;
    c->sessDcprSetupData.compType = CPA_DC_DEFLATE;
    //c->sessDcprSetupData.fileType = CPA_DC_FT_OTHER;
    c->sessDcprSetupData.autoSelectBestHuffmanTree = CPA_TRUE;
    c->sessDcprSetupData.checksum = CPA_DC_CRC32;

}
/*
    Function:

        launch_ctx

    Description:

        Context should be properly initialized with source SGL already populated
        This function will initialize & alloc session and memory buffers for the CTX

    Parameters:

        ctx     - Ptr to the context
        sgls    - Ptr to the sgl container with all necessary SGLS

    Return:

        Result of memory allocation and initialization API calls
*/
CpaStatus launch_ctx(struct context *ctx, struct sgl_container *sgls)
{
    CpaStatus status;
    uint32_t iNum;
    uint32_t sess_size;
    uint32_t ctx_size;

    if (ctx == NULL)
    {
        MG_LOG_PRINT(g_log_fd, "Error: context is null!\n");
        return CPA_STATUS_FAIL;
    }

    iNum = sgls->t_id % numDcInstances_g;

    //
    // Setup session handle
    //
    status = cpaDcGetSessionSize(dcInstances_g[iNum], &(ctx->sessDcprSetupData), &sess_size, &ctx_size);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not get session size!\n");
        return status;
    }
    status = cpaDcGetSessionSize(dcInstances_g[iNum], &(ctx->sessDcprSetupData), &sess_size, &ctx_size);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not get session size!\n");
        return status;
    }


    ctx->sessCprHandle = (CpaDcSessionHandle) qaeMemAllocNUMA(sess_size, ctx->nodeId, BYTE_ALIGNMENT_64);
    if (ctx->sessCprHandle == NULL)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not allocate space for session handle!\n");
        return CPA_STATUS_FAIL;
    }
    ctx->sessDcprHandle = (CpaDcSessionHandle) qaeMemAllocNUMA(sess_size, ctx->nodeId, BYTE_ALIGNMENT_64);
    if (ctx->sessDcprHandle == NULL)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not allocate space for session handle!\n");
        return CPA_STATUS_FAIL;
    }


#ifdef DEBUG_CODE
    pthread_mutex_lock(&mem_mutex);
    g_alloc++;
    pthread_mutex_unlock(&mem_mutex);
#endif

    status = cpaDcInitSession(dcInstances_g[iNum],
                              ctx->sessCprHandle,
                              &(ctx->sessCprSetupData),
                              sgls->context_sgl,
                              NULL);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not initialize session\n");
        return status;
    }
    status = cpaDcInitSession(dcInstances_g[iNum],
                              ctx->sessDcprHandle,
                              &(ctx->sessDcprSetupData),
                              sgls->context_sgl,
                              NULL);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not initialize session\n");
        return status;
    }

	    

    //
    // Allocate memory for dest/compare buffers in DRAM
    //

    if (ctx->decomp_only) {
        ctx->dest_mem    = (Cpa8U *)calloc(1, ctx->src_data->dcpr_size);
        ctx->compare_mem = (Cpa8U *)calloc(1, ctx->src_data->dcpr_size);
        ctx->mem_size    = ctx->src_data->dcpr_size;
    } else {
        ctx->dest_mem    = (Cpa8U *)calloc(ctx->src_data->file_size, 2);
        ctx->compare_mem = (Cpa8U *)calloc(ctx->src_data->file_size, 2);
	if(ctx->zlibcompare)
		ctx->zlib_mem = (Cpa8U *)calloc(ctx->src_data->file_size, 2);
        ctx->mem_size    = (ctx->src_data->file_size * 2);
    }

    if (ctx->dest_mem == NULL || ctx->compare_mem == NULL)
    {
        MG_LOG_PRINT(g_log_fd, "Failed to allocate context data memory\n");
        return CPA_STATUS_FAIL;
    }

    return status;
}

/*
    Function:

        guard_q_size

    Description

        This function ensures that contexts can only be enqueued if there is space
        If this particular ctx causes the Q to be full, it sets a drain flag
        This drain flag gets unset by deq_ctx when the consumer threads create enough space
        The function will spin on this drain flag until it is unset

    Parameters:

        None

    Return:

        None
*/
static void guard_q_size()
{
    pthread_mutex_lock(&queue_size_mutex);

    if (g_q_size >= MAX_Q_SIZE && !g_draining_q) {
        g_draining_q = true;
    }

    pthread_mutex_unlock(&queue_size_mutex);

    while (g_draining_q) {
        sleep(.3);
    }
}

/*
    Function:

        enq_ctx

    Description:

        Enqueues and context

    Parameters:

        ctx -   Ptr to context to enq

    Return:

        none
*/
void enq_ctx(struct context *ctx)
{
    guard_q_size();

    pthread_mutex_lock(&queue_mutex);
    TAILQ_INSERT_TAIL(&tailq_head, ctx, entries);

    pthread_mutex_lock(&queue_size_mutex);
    g_q_size++;
    pthread_mutex_unlock(&queue_size_mutex);

    pthread_mutex_unlock(&queue_mutex);
}

/*
    Function:

        deq_ctx

    Description:

        Dequeues a context; Responsible for letting producer thd know when Q has space again

    Parameters:

        none

    Return:

        Ptr to context that was dequeued
*/
struct context *deq_ctx()
{
    struct context *ctx;

    pthread_mutex_lock(&queue_mutex);

    ctx = tailq_head.tqh_first;
    if (ctx == NULL) {
        pthread_mutex_unlock(&queue_mutex);
        return ctx;
    }

    TAILQ_REMOVE(&tailq_head, ctx, entries);

    pthread_mutex_lock(&queue_size_mutex);

    g_q_size--;

    if (g_draining_q && g_q_size <= DRAIN_Q_SIZE) {
        g_draining_q = false;
    }

    pthread_mutex_unlock(&queue_size_mutex);

    pthread_mutex_unlock(&queue_mutex);

    return ctx;
}
