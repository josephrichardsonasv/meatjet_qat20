#include "cpr.h"
#include "context.h"
#include "buf_handler.h"
#include "meatjet.h"
#include <zlib.h>

#ifdef DEBUG_CODE
Cpa32U g_alloc;
Cpa32U g_free;
pthread_mutex_t mem_mutex;

uint64_t g_of_cnt;
pthread_mutex_t of_mutex;
#endif

bool g_all_ctx_created = false;
pthread_t mg_threads[MAX_THREAD_COUNT];
extern CpaInstanceHandle *dcInstances_g;
extern Cpa16U numDcInstances_g;
extern FILE *g_log_fd;

/*
    Function:

        calc_dcpr_size

    Description:

        Calculates the ultimate size of the cleartext files post-dcpr

    Parameters:

        src - src_data struct that contains source file info

    Return:

        size of the cleartext file
 */
static size_t calc_dcpr_size(struct src_data *src)
{
    int ret;
    size_t dcpr_size;
    unsigned char *output;
    z_stream s;

    s.zalloc = Z_NULL;
    s.zfree = Z_NULL;
    s.opaque = Z_NULL;

    s.avail_in = src->file_size;
    s.next_in = src->src_mem;

    ret = inflateInit2(&s, -15);
    if (ret != Z_OK) {
        MG_LOG_PRINT(g_log_fd, "ERROR: could not calculate the dcpr_size of the src file [%s]\n", src->filename);
        return ret;
    }

    output = (unsigned char *)malloc(DEFAULT_BUF_SIZE);

    while (s.avail_in > 0 && ret != Z_DATA_ERROR)
    {
        s.avail_out = DEFAULT_BUF_SIZE;
        s.next_out = output;

        ret = inflate(&s, Z_NO_FLUSH);
        switch (ret)
        {
            case Z_NEED_DICT:
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                ret = Z_DATA_ERROR;     /* and fall through */
                break;
        }
    }

    if (ret == Z_STREAM_END) {
        dcpr_size = s.total_out;
    } else {
        // we still output a size in case we are intentionally trying to run a broken file through
        // and we want to see the hardware's reaction. Figuring x4 is a good enough start
        dcpr_size = src->file_size * 4;
    }
    s.next_out = output;

    free(output);
    (void)inflateEnd(&s);
    
    return dcpr_size;
}

/*
    Function:

        threads_init

    Description:

        Creates the requested number of threads, along with an ID

    Parameters:

        threads -   Number of threads to create

    Return:

        none
*/
static void threads_init(uint32_t threads)
{
    for (uint32_t i = 0; i < threads; i++)
    {
        int *id = (int *)calloc(1,sizeof(int));
        *id = i;
        pthread_create(&mg_threads[i], NULL, cpr_thread_entry, id);
    }
}

/*
    Function:

        threads_join

    Description:

        Sets a global flag indicating that all contexts have been created. Joins threads

    Parameters:

        threads -   Number of threads to join

    Return:

        none
*/
static void threads_join(uint32_t threads)
{
    g_all_ctx_created = true;

    for (uint32_t i = 0; i < threads; i++)
    {
        pthread_join(mg_threads[i], NULL);
    }
}

/*
    Function:

        create_src_data

    Description:

        Allocates space for the source data, copies data from a file into mem buf, and inits members

    Parameters:

        filename    -   File from which to read data
        decomp_only -   Decomp only?

    Return:

        Pointer to the newly created src_data struct
*/
static struct src_data *create_src_data(char *filename, bool decomp_only)
{
    FILE *fd;
    struct src_data *src;

    src = (struct src_data *)calloc(1, sizeof(struct src_data));

    strncpy(src->filename, filename, MAX_FILE_LEN);
    src->file_size = get_file_size(filename);

    src->src_mem = (Cpa8U *)calloc(1, src->file_size);
    if (src->src_mem == NULL) {
        MG_LOG_PRINT(g_log_fd, "Unable to allocate src_mem data!\n");
        free(src);
        return NULL;
    }

    pthread_mutex_init(&(src->src_mutex), NULL);

    fd = fopen(filename, "r");

    if (fread(src->src_mem, 1, src->file_size, fd) != src->file_size)
    {
        MG_LOG_PRINT(g_log_fd, "Could not read all source data from file!\n");
        free(src->src_mem);
        free(src);
        return NULL;
    }

    fclose(fd);

    if (decomp_only) {
        src->dcpr_size = calc_dcpr_size(src);
    }

    return src;
}

/*
    Function:

        decrement_src_ref

    Description:

        Thread-safely decrements the reference count to how many unused contexts still rely on this
        source data. If the ref count reaches zero, all data is freed, and the struct is removed.

    Parameters:

        s   -   Pointer to the src_data struct we are modifying

    Return:

        none
*/
static void decrement_src_ref(struct src_data *s)
{
    Cpa32U ref;
    double pct;

    pthread_mutex_lock(&(s->src_mutex));

    ref = s->ref_count;

    if (ref == 0) {
        MG_LOG_PRINT(g_log_fd, "Trying to decrement src data that's already at zero! Something is wrong\n");
        pthread_mutex_unlock(&(s->src_mutex));
        return;
    }

    ref--;

    if (ref % 2000 == 0) {
        pct = (ref/(s->orig_ref_count*1.0))*100;
        MG_LOG_PRINT(g_log_fd, "Context Update: %u of %u (%.3f%%) remaining for [%s]\n",
                ref, s->orig_ref_count, pct, s->filename);
    }

    if (ref == 0) {
        MG_LOG_PRINT(g_log_fd, "Source data [%s] has 0 ctx references. Freeing memory\n", s->filename);
        s->ref_count = ref;
        free(s->src_mem);
        pthread_mutex_unlock(&(s->src_mutex));
        pthread_mutex_destroy(&(s->src_mutex));
        //free(s);
        return;
    }

    s->ref_count = ref;

    pthread_mutex_unlock(&(s->src_mutex));
}

/*
    Function:

        create_cpr_lvl_mask

    Description:

        Creates a bitmask for all of the compression levels to be run. The reason
        this is required is to save time, since most compression levels in the QAT
        driver are redundant -- only to match gzip API. We want to run the minimum
        number of levels to achieve 100% coverage.

        Refer to dc_session.c to see how they are mapped for each IP

    Parameters:

        mask    - Ptr to the compression level mask
        opt     - Ptr to the CL options, in case a specific cpr lvl was requested

    Return:

        Total number of contexts that will be run
*/
static uint8_t create_cpr_lvl_mask(uint16_t *mask, struct mg_options *opt)
{
    uint8_t total = 0;
    uint16_t tmp = 0;

    if (opt->decomp_only) {
        *mask = 0xffff;
        return 1;
    }

#if defined(COLETO_CREEK) || defined(CPM17)
    /*
       defined in dc_session.c:

       +-----+-------+--------+
       | LVL | DEPTH | WIN_SZ |
       +-----+-------+--------+
       |  1  |   1   |  32K   |
       +-----+-------+--------+
       |  3  |   4   |  16K   |
       +-----+-------+--------+
       |  4  |   8   |  16K   |
       +-----+-------+--------+
       |  7  |   16  |  16K   |
       +-----+-------+--------+
    */

    tmp |= (1 << 1);    // depth = 1
    tmp |= (1 << 2);    // depth = 4
    tmp |= (1 << 3);    // depth = 8
    tmp |= (1 << 4);    // depth = 16

    total = 4;

#elif defined(CPM18)

    tmp |= (1 << 1);    // depth = 1
    tmp |= (1 << 2);    // depth = 4
    tmp |= (1 << 3);    // depth = 8
    tmp |= (1 << 4);    // depth = 16
    tmp |= (1 << 5);    // depth = 128

    total = 5;

#else
    // DEFAULT: just do all contexts to be safe
    tmp = 0x3FE;    // 0011 1111 1110
    total = 9;
#endif

    // If a specific compression level was passed in via CL...
    if (opt->min_cpr_lvl == opt->max_cpr_lvl) {
        tmp = 0;
        tmp |= (1 << opt->min_cpr_lvl);
        total = 1;
    }

    *mask = tmp;

#ifdef DEBUG_CODE
    MG_LOG_PRINT(g_log_fd, "MASK: 0x%x\n", *mask);
    MG_LOG_PRINT(g_log_fd, "Total levels: %d\n", total);
#endif

    return total;
}

/*
    Function:

        build_underflow_ctx_list

    Description:

        Initializes the source data for each file. Then goes and creates all of the
        necessary contexts at the appropriate underflow points, and enqueues them for
        the consumer threads to pull off and churn through

        Primary CTX producer

    Parameters:

        opt -   Ptr to the command line options struct

    Return:

        none
*/
static void build_underflow_ctx_list(struct mg_options *opt, struct src_data *s)
{
    uint32_t IBC_STEP = 1;

    uint64_t id = 0;
    uint32_t start_ibc;
    uint32_t end_ibc;
    uint32_t num_ibc;
    struct context *ctx;

    uint8_t total_cpr_lvls;
    uint16_t cpr_lvl_mask;

    num_ibc = 0;

    if (opt->ibc_step) {
        MG_LOG_PRINT(g_log_fd, "Setting ibc step to %u\n", opt->ibc_step);
        IBC_STEP = opt->ibc_step;
    }

    start_ibc = MIN_IBC_VALUE;
    end_ibc = s->file_size;

    if (opt->ibc > 0) {
        start_ibc = opt->ibc;
        end_ibc = opt->ibc;
    }
    else if (end_ibc <= start_ibc) {
        start_ibc = end_ibc;
    }

    num_ibc = ((end_ibc - start_ibc) / IBC_STEP) + 1;

    if (!opt->dynamic_only && !opt->static_only) {
        num_ibc *= 2;
    }

    // Populate the compression level mask
    total_cpr_lvls = create_cpr_lvl_mask(&cpr_lvl_mask, opt);

    s->ref_count = total_cpr_lvls * num_ibc;
    s->orig_ref_count = s->ref_count;

    for (uint32_t cpr_lvl = opt->min_cpr_lvl; cpr_lvl <= opt->max_cpr_lvl; cpr_lvl++)
    {
        // Skip this compression level if it's not in the mask
        if (((cpr_lvl_mask >> cpr_lvl) & 1) == 0) {
            continue;
        }

        for (uint32_t ibc = start_ibc; ibc <= end_ibc; ibc += IBC_STEP)
        {
            if (!opt->dynamic_only) {
                ctx = create_ctx(opt, id++);
		if (!opt->stateless){
                    fill_ctx_sess(ctx, cpr_lvl, CPA_DC_HT_STATIC, CPA_DC_STATEFUL, 7);
		}
                else {
                    fill_ctx_sess(ctx, cpr_lvl, CPA_DC_HT_STATIC, CPA_DC_STATELESS, 7);
                }    
                ctx->src_data = s;
                ctx->uf_ibc = ibc;
                enq_ctx(ctx);
            }

            if (!opt->static_only) {
                ctx = create_ctx(opt, id++);
		if (!opt->stateless){
                    fill_ctx_sess(ctx, cpr_lvl, CPA_DC_HT_FULL_DYNAMIC, CPA_DC_STATEFUL, 7);
		}
                else {
                    fill_ctx_sess(ctx, cpr_lvl, CPA_DC_HT_FULL_DYNAMIC, CPA_DC_STATELESS, 7);
                }
                ctx->src_data = s;
                ctx->uf_ibc = ibc;
                enq_ctx(ctx);
            }
        }
    }

    MG_LOG_PRINT(g_log_fd, "Total underflow contexts created: %lu\n\t(ref count: %u)\n", id, s->orig_ref_count);
}

/*
    Function:

        build_overflow_ctx_list

    Description:

        Initializes the source data for each file. Then goes and creates all of the
        necessary contexts at the appropriate overflow points, and enqueues them for
        the consumer threads to pull off and churn through

        Primary CTX producer

    Parameters:

        opt -   Ptr to the command line options struct

    Return:

        none
*/
static void build_overflow_ctx_list(struct mg_options *opt, struct src_data *s)
{
    uint32_t OBS_STEP = 1;

    uint64_t id = 0;
    struct context *ctx;
    uint32_t num_obs;
    uint32_t start_obs;
    uint32_t end_obs;

    uint8_t total_cpr_lvls;
    uint16_t cpr_lvl_mask;

    if (opt->obs_step) {
        MG_LOG_PRINT(g_log_fd, "Setting obs step to %u\n", opt->obs_step);
        OBS_STEP = opt->obs_step;
    }

    num_obs = 0;

    // If OBS is manually chosen via CL, only create one context
    if (opt->obs) {
        num_obs = 1;
        start_obs = opt->obs;
        end_obs = opt->obs;

    // Otherwise, go through each OBS and create a new context
    } else {
        start_obs = MIN_OBS_VALUE;
        end_obs = s->file_size / 2;

        if (end_obs <= MIN_OBS_VALUE) {
            end_obs = MIN_OBS_VALUE;
            num_obs = 1;
        } else {
            uint32_t j;

            j = (end_obs - start_obs);

            while (j >= OBS_STEP)
            {
                j -= OBS_STEP;
                num_obs++;
            }

            num_obs++;
        }
    }

    if (!opt->dynamic_only && !opt->static_only) {
        num_obs *= 2;
    }

    total_cpr_lvls = create_cpr_lvl_mask(&cpr_lvl_mask, opt);

    s->ref_count = total_cpr_lvls * num_obs;
    s->orig_ref_count = s->ref_count;

    MG_LOG_PRINT(g_log_fd, "Expected overflow contexts created for file [%s]: %u\n", s->filename, s->orig_ref_count);

    for (uint32_t cpr_lvl = opt->min_cpr_lvl; cpr_lvl <= opt->max_cpr_lvl; cpr_lvl++)
    {
        // Skip this compression level if it's not in the mask
        if (((cpr_lvl_mask >> cpr_lvl) & 1) == 0) {
            continue;
        }

        for (uint32_t obs = start_obs; obs <= end_obs; obs += OBS_STEP)
        {
            // STATIC
            if (!opt->dynamic_only) {
                ctx = create_ctx(opt, id++);
		if (!opt->stateless){
              	    fill_ctx_sess(ctx, cpr_lvl, CPA_DC_HT_STATIC, CPA_DC_STATEFUL, 7);
		}
		else {
		    fill_ctx_sess(ctx, cpr_lvl, CPA_DC_HT_STATIC, CPA_DC_STATELESS, 7);
		}	
                ctx->src_data = s;
                ctx->obs = obs;
                enq_ctx(ctx);
            }

            // DYNAMIC
            if (!opt->static_only) {
                ctx = create_ctx(opt, id++);
		if (!opt->stateless){
                    fill_ctx_sess(ctx, cpr_lvl, CPA_DC_HT_FULL_DYNAMIC, CPA_DC_STATEFUL, 7);
     	        }
	        else {
	            fill_ctx_sess(ctx, cpr_lvl, CPA_DC_HT_FULL_DYNAMIC, CPA_DC_STATELESS, 7);
	        }
                ctx->src_data = s;
                ctx->obs = obs;
                enq_ctx(ctx);
            }
        }
    }

    MG_LOG_PRINT(g_log_fd, "Total overflow contexts created for file [%s]: %lu\n\t(ref count: %u)\n",
            s->filename, id, s->orig_ref_count);
}

/*
    Function:

        build_ctx_list

    Description:

        MUX for which type of context to be created (UF or OF)

    Parameters:

        opt -   Ptr to the command line options struct

    Returns:

        none
*/
static void build_ctx_list(struct mg_options *opt, struct src_data *s)
{

    if (opt->underflow) {
        build_underflow_ctx_list(opt, s);
    } else {
        build_overflow_ctx_list(opt, s);
    }
}

/*
    Function:

        init_sgl_mem (static)

    Description:

        Initializes and allocates memory for all necessary SGLs. Src, Dest, Context
        This is done once PER THREAD, and every context that is deq'd in that thread
        will reuse these slabs.

    Parameters:

        sgls    - Pointer to the struct that will hold all ptrs to SGLs

    Return:

        Returns the result of the allocations. CPA_STATUS_SUCCESS/FAIL
*/
static CpaStatus init_sgl_mem(struct sgl_container *sgls)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    uint32_t meta_size;
    uint32_t iNum;

    sgls->src_sgl = (CpaBufferList *)calloc(1, sizeof(CpaBufferList));
    sgls->dest_sgl = (CpaBufferList *)calloc(1, sizeof(CpaBufferList));
    sgls->context_sgl = (CpaBufferList *)calloc(1, sizeof(CpaBufferList));

    iNum = sgls->t_id % numDcInstances_g;

    //
    // Setup src/dest/context SGL's
    //
    cpaDcBufferListGetMetaSize(dcInstances_g[iNum], 1, &meta_size);

    status = mg_build_sgl(sgls->src_sgl, 0, 1, DEFAULT_BUF_SIZE, meta_size);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not build src sgl!\n");
        return status;
    }

    status = mg_build_sgl(sgls->dest_sgl, 0, 1, DEFAULT_BUF_SIZE, meta_size);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not build dest sgl!\n");
        return status;
    }

    /*
        NOTE: This SGL allocation (context_sgl) is typically dependent on the
            compression configuration (window size, compression level), which will
            vary for each context potentially. Since we are trying to allocate just
            once per thread, rather than per ctx, we are defaulting to the max size,
            32k, found in the driver source
    */
    status = mg_build_sgl(sgls->context_sgl, 0, 2, DC_MAX_HISTORY_SIZE, meta_size);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not build context sgl!\n");
        return status;
    }

    return status;
}

/*
    Function:

        free_sgls

    Description:

        Frees all SGLs, each being thread-specific, not CTX specific

    Parameters:

        sgls    -   Ptr to the struct containing all the SGLs

    Return:

        none
*/
static void free_sgls(struct sgl_container *sgls)
{
    // Free the SGL's pinned memory before structs containing them
    mg_free_sgl(sgls->src_sgl);
    mg_free_sgl(sgls->dest_sgl);
    mg_free_sgl(sgls->context_sgl);

    free(sgls->src_sgl);
    free(sgls->dest_sgl);
    free(sgls->context_sgl);
}

/*
*/
static void shutdown_services()
{
    stopDcServices();
    icp_sal_userStop();
    qaeMemDestroy();
}

static void print_summary(struct src_data **list, int num_files)
{
    MG_LOG_PRINT(g_log_fd, "\n*******************************\n");
    MG_LOG_PRINT(g_log_fd, "***** Meatgrinder Summary *****\n");
    MG_LOG_PRINT(g_log_fd, "*******************************\n\n");
    MG_LOG_PRINT(g_log_fd, "    Total Files: %d\n\n", num_files);

    for (int i = 0; i < num_files; i++)
    {
        MG_LOG_PRINT(g_log_fd, "    [%s] %s\n", list[i]->fail_count == 0 ? "PASS" : "FAIL", list[i]->filename);
    }

    MG_LOG_PRINT(g_log_fd, "\n");
}

static CpaStatus check_fail(struct src_data **list, int num_files)
{
    for (int i = 0; i < num_files; i++)
    {
        if (list[i]->fail_count) {
            return CPA_STATUS_FAIL;
        }
    }

    return CPA_STATUS_SUCCESS;
}


/*
    Function:

        cpr_start

    Description:

        Main entry point for all QAT & compression work. Inits data for the driver & kicks off process

    Parameters:

        opts    -   Ptr to command line options struct

    Return:

        CPA_STATUS_FAIL if any failures were detected, otherwise CPA_STATUS_SUCCESS
*/
CpaStatus cpr_start(struct mg_options *opts)
{
    CpaStatus status;
    int num_files;
    char **file_list;
    struct src_data **src_list;

#ifdef DEBUG_CODE
    g_alloc = g_free = 0;
    pthread_mutex_init(&mem_mutex, NULL);

    g_of_cnt = 0;
    pthread_mutex_init(&of_mutex, NULL);
#endif

    if (opts == NULL) MG_LOG_PRINT(g_log_fd, "PASS\n");

    status = qaeMemInit();
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not init qaeMem\n");
        shutdown_services();
        exit(status);
    }

    status = icp_sal_userStartMultiProcess("SSL", CPA_FALSE);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not start userspace proc\n");
        shutdown_services();
        exit(status);
    }

    status = startDcServices(DYNAMIC_BUFFER_AREA);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not initialize shit\n");
        shutdown_services();
        exit(status);
    }

    if (CPA_STATUS_SUCCESS != dcCreatePollingThreadsIfPollingIsEnabled())
    {
        MG_LOG_PRINT(g_log_fd, "Error: could not create polling threads\n");
        shutdown_services();
        exit(CPA_STATUS_FAIL);
    }

    ctx_init();

    threads_init(opts->threads);

    //
    // Build the entire list of files that will be tested
    //

    // Get the total number of files we will be running through
    if (opts->use_dir) {
        num_files = get_num_files(opts->dir);
    } else {
        num_files = 1;
    }

    // Allocate space for the file list
    file_list = (char **)calloc(num_files, sizeof(char *));
    for (int i = 0; i < num_files; i++)
    {
        file_list[i] = (char *)calloc(1, MAX_FILE_LEN);
    }
    src_list = (struct src_data **)calloc(num_files, sizeof(struct src_data *));

    // Populate the file list with the appropriate file name
    if (opts->use_dir) {
        populate_file_list(&file_list, opts->dir, 0);
    } else {
        strcpy(file_list[0], opts->input_file);
    }

    // Build a context list for each file
    for (int i = 0; i < num_files; i++)
    {
        src_list[i] = create_src_data(file_list[i], opts->decomp_only);
        build_ctx_list(opts, src_list[i]);

        sleep(.2);
    }

    threads_join(opts->threads);

    stopDcServices();
    icp_sal_userStop();
    qaeMemDestroy();

#ifdef DEBUG_CODE
    MG_LOG_PRINT(g_log_fd, "alloc: %u\n", g_alloc);
    MG_LOG_PRINT(g_log_fd, "free: %u\n", g_free);
    MG_LOG_PRINT(g_log_fd, "overflows: %lu", g_of_cnt);

    pthread_mutex_destroy(&mem_mutex);
    pthread_mutex_destroy(&of_mutex);
#endif

    print_summary(src_list, num_files);
    status = check_fail(src_list, num_files);

    // Free the file list memory
    for (int i = 0; i < num_files; i++)
    {
        free(file_list[i]);
        free(src_list[i]);
    }

    free(file_list);
    free(src_list);

    return status;
}

/*
    Function:

        cpr_thread_entry

    Description:

        This is the entry point for the consumer threads. They will allocate SGL memory
        upon entry, and then begin picking contexts off of the Q, and run them through
        meatjet.

    Parameters:

        arg_id  -   Ptr to a thread ID number. This was calloc'd and needs freeing

    Return:

        none
*/
void *cpr_thread_entry(void *arg_id)
{
    CpaStatus status;
    uint32_t t_id;
    struct context *ctx;
    struct sgl_container *sgls;

    // Do some evil ptr hax to save thread id
    t_id = *((int *)arg_id);
    free(arg_id);

    // Initialize the container, and then all memory within
    sgls = (struct sgl_container *)calloc(1, sizeof(struct sgl_container));
    sgls->t_id = t_id;

    status = init_sgl_mem(sgls);
    if (status != CPA_STATUS_SUCCESS)
    {
        MG_LOG_PRINT(g_log_fd, "Error: Could not initialize thread-specific phys memory!\n");
        free_sgls(sgls);
        return NULL;
    }

    // Continuously loop on the Q until all contexts are completed
    while (!g_all_ctx_created || !TAILQ_EMPTY(&tailq_head))
    {
        // Try and acquire context from the queue
        ctx = deq_ctx();
        if (ctx == NULL)
        {
            sleep(.1); continue;
        }

        launch_ctx(ctx, sgls);

        status = meatjet(ctx, sgls);
        if (status != CPA_STATUS_SUCCESS)
        {
            ctx->src_data->fail_count++;
        }

        decrement_src_ref(ctx->src_data);

        free_ctx(ctx, sgls);
    }

    free_sgls(sgls);
    free(sgls);

    return NULL;
}
