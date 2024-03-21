#include <time.h>
#include "meatjet.h"

extern CpaInstanceHandle *dcInstances_g;
extern Cpa16U numDcInstances_g;
extern FILE *g_log_fd;

#ifdef DEBUG_CODE
extern uint64_t g_of_cnt;
extern pthread_mutex_t of_mutex;
#endif
static pthread_mutex_t mg_log_mutex;

/*
    Function:

        zlib_inflate

    Description:

        Decompresses an entire file with zlib

    Parameters:

        ctx - Ptr to a context struct

    Return:


*/
static uint32_t zlib_inflate(struct context *ctx)
{
    int ret;
    z_stream strm;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = inflateInit2(&strm, -15);
    if (ret != Z_OK)
        return -ret;

    strm.avail_in = ctx->src_data->file_size;
    strm.next_in = ctx->src_data->src_mem;

    strm.avail_out = ctx->mem_size;
    strm.next_out = ctx->compare_mem;
    ret = inflate(&strm, Z_NO_FLUSH);    /* no bad return value */

    /* clean up and return */
    (void)inflateEnd(&strm);

    return strm.total_out;
}


/*
    Function:

        zlib_compare

    Description:
	
	Compares HW and SW Decompression results via CRC 


    Parameters:

        ctx     -   Ptr to the context that has all cfg info
        status  -   Status of the job

    Return:

        Status of the compress/decompress
*/
static uint32_t zlib_compare(struct context *ctx, CpaStatus status){

        //
        // Get results with ZLIB
        //
	z_stream strm;

	/* allocate deflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ctx->zlib_results.status= inflateInit2(&strm, -15);
	if (ctx->zlib_results.status != Z_OK)
		return -ctx->zlib_results.status;

	strm.avail_in = ctx->mem_size;
	strm.next_in = ctx->dest_mem;

	strm.avail_out = ctx->mem_size;
	strm.next_out = ctx->zlib_mem;
	ctx->zlib_results.status = inflate(&strm, Z_NO_FLUSH);    /* no bad return value */

	/* clean up and return */
	(void)inflateEnd(&strm);

	ctx->zlib_results.size = strm.total_out;
        
	ctx->zlib_results.crc32 = calc_crc32(0, ctx->zlib_mem, ctx->zlib_results.size);

        // Compare Memory
        if ((status != CPA_STATUS_SUCCESS) || (memcmp(ctx->compare_mem, ctx->zlib_mem, ctx->dcpr_produced))) {
            MG_LOG_PRINT(g_log_fd, "\n\n\t******** SW DATA COMPARE ERROR ********\n");
            mg_log(ctx, DC_FAIL_DATA);
            return CPA_STATUS_FAIL;
        }

        // Compare Total Size
        if (ctx->zlib_results.size != ctx->dcpr_produced) {
            MG_LOG_PRINT(g_log_fd, "\n\n\t******** INCORRECT SW DECOMP SIZE COMPARE********\n");
            mg_log(ctx, DC_FAIL_SIZE);
            return CPA_STATUS_FAIL;
        }

        // Compare CRC
        if (ctx->zlib_results.crc32 != ctx->dcpr_results.checksum) {
            MG_LOG_PRINT(g_log_fd, "\n\n\t******** INCORRECT SW CRC CHECKSUM COMPARE ********\n");
            mg_log(ctx, DC_FAIL_CRC);
            return CPA_STATUS_FAIL;
        }

	return status;	

}


/*
    Function:

        meatjet

    Description:

        Main workhorse of the program. Compresses, decompresses, and compares.
        Responsible for ensuring overflows/underflow/restores occur accordingly.
        This function should do no memory management (free/alloc), and should only move data around

    Parameters:

        ctx     -   Ptr to the context that has all cfg info
        sgls    -   Ptr to SGLs which are the necessary memory slabs

    Return:

        Status of the compress/decompress
*/
CpaStatus meatjet(struct context *ctx, struct sgl_container *sgls)
{
    CpaStatus status;
    CpaDcFlush flush;
    CpaDcOpData opData = {}; 
    size_t src_file_size;
    size_t job_size;
    size_t data_copied;
    Cpa32U iNum;
    Cpa32U actual_obs;
    bool target_overflow_complete;
    bool target_underflow_complete;

#ifdef DEBUG_CODE
    uint64_t of_cnt = 0;
#endif

    /*
        1) Compress
            - send in entire source in 64KB chunks
            - produced data is copied to the mem buffer dest_mem
        2) Decompress
            - send in entire compressed data in 64KB chunks
            - produced data is copied to the mem buffer compare_mem
        3) Error check & byte validation
            - CRC comparison
            - Compare src with compare memory buffers
    */

    status = CPA_STATUS_FAIL;

    // Set initial values for the ctx targets
    target_overflow_complete = ctx->underflow;
    target_underflow_complete = !target_overflow_complete;

    iNum = sgls->t_id % numDcInstances_g;
    src_file_size = ctx->src_data->file_size;

    ctx->cpr_produced = 0;
    ctx->cpr_consumed = 0;

    ctx->dcpr_produced = 0;
    ctx->dcpr_consumed = 0;

    //
    // Decompression-only Flow
    //

    if (ctx->decomp_only)
    {
        uint32_t z_size;
        uint32_t crc32;

        while (ctx->dcpr_consumed < src_file_size || ctx->dcpr_results.status == CPA_DC_OVERFLOW)
        {
            // Set the potential underflow IBC condition
            if (((ctx->dcpr_consumed + DEFAULT_BUF_SIZE) >= ctx->uf_ibc) && !target_underflow_complete) {
                job_size = (ctx->uf_ibc - ctx->dcpr_consumed);
                target_underflow_complete = true;
            } else {
                job_size = DEFAULT_BUF_SIZE;
            }

            // Set the job size to compress, and set the slice_final type
            if ((job_size + ctx->dcpr_consumed) < src_file_size) {
                flush = CPA_DC_FLUSH_SYNC;
            } else {
                job_size = (src_file_size - ctx->dcpr_consumed);
                flush = CPA_DC_FLUSH_FINAL;
            }

            // Set the target overflow condition here
            if (!target_overflow_complete) {
                // Check if we're in the proper overflow SGL bucket
                if ((ctx->dcpr_produced + DEFAULT_BUF_SIZE) >= ctx->obs) {
                    actual_obs = ctx->obs - ctx->dcpr_produced;

                    if (actual_obs < MIN_OBS_VALUE) {
                        actual_obs = MIN_OBS_VALUE;
                    }

                    sgls->dest_sgl->pBuffers[0].dataLenInBytes = actual_obs;

                    target_overflow_complete = true;
                }
            }

            // Setup srcSGL with appropriate data
            // Copy the inital data into the src SGL
            data_copied = copy_mem_to_sgl(ctx->src_data->src_mem + ctx->dcpr_consumed,
                                          sgls->src_sgl,
                                          DEFAULT_BUF_SIZE,
                                          job_size);

            if (data_copied != job_size) {
                MG_LOG_PRINT(g_log_fd, "Error: could not copy all data from dest mem to source SGL\n");
                break;
            }

            // Decompress!
            do {
                status = cpaDcDecompressData(dcInstances_g[iNum],
                                         ctx->sessDcprHandle,
                                         sgls->src_sgl,
                                         sgls->dest_sgl,
                                         &(ctx->dcpr_results),
                                         flush,
                                         NULL);
            } while (status == CPA_STATUS_RETRY);
            if (CPA_STATUS_SUCCESS != status && CPA_DC_OVERFLOW != ctx->dcpr_results.status) {
                MG_LOG_PRINT(g_log_fd, "Error: decompression failed with status %d\n", ctx->dcpr_results.status);
                break;
            }

            // Copy the produced data from decompress into the compare buffer
            data_copied = copy_sgl_to_mem(sgls->dest_sgl,
                                          ctx->dest_mem + ctx->dcpr_produced,
                                          DEFAULT_BUF_SIZE,
                                          ctx->dcpr_results.produced);
            if (data_copied != ctx->dcpr_results.produced) {
                MG_LOG_PRINT(g_log_fd, "Error: could not copy all data from dest SGL to compare buffer\n");
                break;
            }

            ctx->dcpr_consumed += ctx->dcpr_results.consumed;
            ctx->dcpr_produced += ctx->dcpr_results.produced;

            if (CPA_DC_OVERFLOW == ctx->dcpr_results.status) {
                for (uint32_t n = 0; n < sgls->dest_sgl->numBuffers; n++) {
                    sgls->dest_sgl->pBuffers[n].dataLenInBytes = DEFAULT_BUF_SIZE;
                }
            }
        }

        //
        // Get results with ZLIB
        //
        z_size = zlib_inflate(ctx);
        crc32 = calc_crc32(0, ctx->compare_mem, z_size);

        // Compare Memory
        if ((status != CPA_STATUS_SUCCESS) || (memcmp(ctx->dest_mem, ctx->compare_mem, ctx->dcpr_produced))) {
            MG_LOG_PRINT(g_log_fd, "\n\n\t******** DATA COMPARE ERROR ********\n");
            mg_log(ctx, DC_FAIL_DATA);
            return CPA_STATUS_FAIL;
        }

        // Compare Total Size
        if (z_size != ctx->dcpr_produced) {
            MG_LOG_PRINT(g_log_fd, "\n\n\t******** INCORRECT DECOMP SIZE ********\n");
            mg_log(ctx, DC_FAIL_SIZE);
            return CPA_STATUS_FAIL;
        }

        // Compare CRC
        if (crc32 != ctx->dcpr_results.checksum) {
            MG_LOG_PRINT(g_log_fd, "\n\n\t******** INCORRECT CRC CHECKSUM ********\n");
            mg_log(ctx, DC_FAIL_CRC);
            return CPA_STATUS_FAIL;
        }

        // Debug mode!
		if (ctx->debug) {
            MG_LOG_PRINT(g_log_fd, "\n\n\t******** Debug Mode: Captures all data ********\n");
            MG_LOG_PRINT(g_log_fd, "\n\n\t******** This may seg fault and crash your system  ********\n");
            mg_log(ctx, DC_DEBUG);
        }


        return status;
    }

    //
    // Compression Flow
    //

    // Continue to compress if:
    //  - there's more data to consume (source file > total consumed)
    //  - overflow was the result of the last job. It's possible that all data has been consumed
    //    and an overflow occurred. This has to effect of residue bytes being stuck in the HW,
    //    so an additional zero-byte job needs to be sent to flush these out
    while (ctx->cpr_consumed < src_file_size || ctx->cpr_results.status == CPA_DC_OVERFLOW)
    {
        // Set potential underflow IBC job sizes
        if (((ctx->cpr_consumed + DEFAULT_BUF_SIZE) >= ctx->uf_ibc) && !target_underflow_complete) {
            job_size = (ctx->uf_ibc - ctx->cpr_consumed);
            target_underflow_complete = true;
        } else {
            job_size = DEFAULT_BUF_SIZE;
        }

        // Set the job size to compress, and set the slice_final type
        if ((job_size + ctx->cpr_consumed) < src_file_size) {
	        if(ctx->sessCprSetupData.sessState == CPA_DC_STATEFUL){
	            opData.flushFlag = CPA_DC_FLUSH_SYNC;
            } else {
		        opData.flushFlag = CPA_DC_FLUSH_FULL;  
            }
        } else {
            job_size = (src_file_size - ctx->cpr_consumed);
            opData.flushFlag = CPA_DC_FLUSH_FINAL;
        }

	//Setting Compress and Verify
    if (CPA_DC_STATELESS == ctx->sessCprSetupData.sessState) {
	    opData.compressAndVerify = CPA_TRUE;
    }
	//opData.compressAndVerifyAndRecover = CPA_FALSE;


        // Copy the inital data into the src SGL
        data_copied = copy_mem_to_sgl(ctx->src_data->src_mem + ctx->cpr_consumed,
                                      sgls->src_sgl,
                                      DEFAULT_BUF_SIZE,
                                      job_size);

        if (data_copied != job_size) {
            MG_LOG_PRINT(g_log_fd, "Error: could not copy all data to the src SGL!\n");
            break;
        }

        // Set the target overflow condition here
        if (!target_overflow_complete) {
            // Check if we're in the proper overflow SGL bucket
            if ((ctx->cpr_produced + DEFAULT_BUF_SIZE) >= ctx->obs) {
                actual_obs = ctx->obs - ctx->cpr_produced;

                if (actual_obs < MIN_OBS_VALUE) {
                    actual_obs = MIN_OBS_VALUE;
                }

                sgls->dest_sgl->pBuffers[0].dataLenInBytes = actual_obs;

                target_overflow_complete = true;
            }
        }
	
        // Compress!
        do {
            status = cpaDcCompressData2(dcInstances_g[iNum],
                                   ctx->sessCprHandle,
                                   sgls->src_sgl,
                                   sgls->dest_sgl,
				                   &opData,
                                   &(ctx->cpr_results),
                                   NULL);
        } while (status == CPA_STATUS_RETRY);

        // Enter here for non-overflow failures
        if (CPA_STATUS_SUCCESS != status && CPA_DC_OVERFLOW != ctx->cpr_results.status) {
            MG_LOG_PRINT(g_log_fd, "Compress Error: status %d [%s]\n", status, ctx->src_data->filename);
            break;
        }

        data_copied = copy_sgl_to_mem(sgls->dest_sgl,
                                      ctx->dest_mem + ctx->cpr_produced,
                                      DEFAULT_BUF_SIZE,
                                      ctx->cpr_results.produced);

        if (data_copied != ctx->cpr_results.produced) {
            MG_LOG_PRINT(g_log_fd, "Error: could not copy all data from dest SGL to memory buffer\n");
            break;
        }

        if (ctx->cpr_results.status == CPA_DC_OVERFLOW) {

#ifdef DEBUG_CODE
            of_cnt++;
#endif

            sgls->dest_sgl->pBuffers[0].dataLenInBytes = DEFAULT_BUF_SIZE;

            memset(sgls->src_sgl->pBuffers[0].pData,  0, DEFAULT_BUF_SIZE);
            memset(sgls->dest_sgl->pBuffers[0].pData, 0, DEFAULT_BUF_SIZE);
        }

        ctx->cpr_consumed += ctx->cpr_results.consumed;
        ctx->cpr_produced += ctx->cpr_results.produced;
    }

    // Set the dest buffer back to the original size for verification
    sgls->dest_sgl->pBuffers[0].dataLenInBytes = DEFAULT_BUF_SIZE;

    //
    // Decompression - Verification
    //
    while (ctx->dcpr_consumed < ctx->cpr_produced || ctx->dcpr_results.status == CPA_DC_OVERFLOW)
    {
        if ((ctx->cpr_produced - ctx->dcpr_consumed) > DEFAULT_BUF_SIZE) {
            job_size = DEFAULT_BUF_SIZE;
            flush = CPA_DC_FLUSH_SYNC;
        } else {
            job_size = (ctx->cpr_produced - ctx->dcpr_consumed);
            flush = CPA_DC_FLUSH_FINAL;
        }

        // Setup srcSGL with appropriate data
        data_copied = copy_mem_to_sgl(ctx->dest_mem + ctx->dcpr_consumed,
                                      sgls->src_sgl,
                                      DEFAULT_BUF_SIZE,
                                      job_size);
        if (data_copied != job_size) {
            MG_LOG_PRINT(g_log_fd, "Error: could not copy all data from dest mem to source SGL\n");
            break;
        }

        // Decompress!
        do {
            status = cpaDcDecompressData(dcInstances_g[iNum],
                                     ctx->sessDcprHandle,
                                     sgls->src_sgl,
                                     sgls->dest_sgl,
                                     &(ctx->dcpr_results),
                                     flush,
                                     NULL);
        } while (CPA_STATUS_RETRY == status);

        if (CPA_STATUS_SUCCESS != status && CPA_DC_OVERFLOW != ctx->dcpr_results.status) {
            MG_LOG_PRINT(g_log_fd, "Error: decompression failed with status %d\n", ctx->dcpr_results.status);
            break;
        }

        // Copy the produced data from decompress into the compare buffer
        data_copied = copy_sgl_to_mem(sgls->dest_sgl,
                                      ctx->compare_mem + ctx->dcpr_produced,
                                      DEFAULT_BUF_SIZE,
                                      ctx->dcpr_results.produced);
        if (data_copied != ctx->dcpr_results.produced) {
            MG_LOG_PRINT(g_log_fd, "Error: could not copy all data from dest SGL to compare buffer\n");
            break;
        }

        ctx->dcpr_consumed += ctx->dcpr_results.consumed;
        ctx->dcpr_produced += ctx->dcpr_results.produced;

        if (CPA_DC_OVERFLOW == ctx->dcpr_results.status) {
            for (uint32_t n = 0; n < sgls->dest_sgl->numBuffers; n++) {
                sgls->dest_sgl->pBuffers[n].dataLenInBytes = DEFAULT_BUF_SIZE;
            }
        }
    }

    // Compare Memory
    if (memcmp(ctx->src_data->src_mem, ctx->compare_mem, src_file_size)) {
        MG_LOG_PRINT(g_log_fd, "\n\n\t******** DATA COMPARE ERROR ********\n");
        mg_log(ctx, DC_FAIL_DATA);
        return CPA_STATUS_FAIL;
    }

    // Compare CRC
    if (ctx->cpr_results.checksum != ctx->dcpr_results.checksum) {
        MG_LOG_PRINT(g_log_fd, "\n\n\t******** CRC CHECKSUM ERROR ********\n");
        mg_log(ctx, DC_FAIL_CRC);
        return CPA_STATUS_FAIL;
    }

    // Debug mode!
    if (ctx->debug) {
        MG_LOG_PRINT(g_log_fd, "\n\n\t******** Debug Mode: Captures all data ********\n");
        MG_LOG_PRINT(g_log_fd, "\n\n\t******** Too many contexts may seg fault and crash your system  ********\n");
        mg_log(ctx, DC_DEBUG);
    }

    //SW Decompress some % of the time.	
    if(ctx->zlibcompare) {
	srand(time(0));
	if( (unsigned)(rand() % 100) < ctx->zlibcompare )  
	    zlib_compare(ctx, status);
    }

#ifdef DEBUG_CODE
    pthread_mutex_lock(&of_mutex);
    g_of_cnt += of_cnt;
    pthread_mutex_unlock(&of_mutex);
#endif

    return status;
}

/*
static char **translate_err_code(CpaStatus status)
{
    char *test_status;

    test_status = (char *)calloc(1, 48);

    switch (status)
    {
    case CPA_DC_OK:
        test_status = "CPA_DC_OK";
        break;
    case CPA_DC_INVALID_BLOCK_TYPE:
        test_status = "CPA_DC_INVALID_BLOCK_TYPE";
        break;
    case CPA_DC_BAD_STORED_BLOCK_LEN:
        test_status = "CPA_DC_BAD_STORED_BLOCK_LEN";
        break;
    case CPA_DC_TOO_MANY_CODES:
        test_status = "CPA_DC_TOO_MANY_CODES";
        break;
    case CPA_DC_INCOMPLETE_CODE_LENS:
        test_status = "CPA_DC_INCOMPLETE_CODE_LENS";
        break;
    case CPA_DC_REPEATED_LENS:
        test_status = "CPA_DC_REPEATED_LENS";
        break;
    case CPA_DC_MORE_REPEAT:
        test_status = "CPA_DC_MORE_REPEAT";
        break;
    case CPA_DC_BAD_LITLEN_CODES:
        test_status = "CPA_DC_BAD_LITLEN_CODES";
        break;
    case CPA_DC_BAD_DIST_CODES:
        test_status = "CPA_DC_BAD_DIST_CODES";
        break;
    case CPA_DC_INVALID_CODE:
        test_status = "CPA_DC_INVALID_CODE";
        break;
    case CPA_DC_INVALID_DIST:
        test_status = "CPA_DC_INVALID_DIST";
        break;
    case CPA_DC_OVERFLOW:
        test_status = "CPA_DC_OVERFLOW";
        break;
    case CPA_DC_SOFTERR:
        test_status = "CPA_DC_SOFTERR";
        break;
    case CPA_DC_FATALERR:
        test_status = "CPA_DC_FATALERR";
        break;
    default:
        test_status = "Unknown Status";
        break;
    }

    return &test_status;
}
*/

/*
    Function:

        mg_log

    Description:

        Logs all necessary data upon failure in a dir that will be created automatically

    Parameters:

        ctx -   Ptr to ctx for all necessary information

    Return:

        none
*/
void mg_log(struct context *ctx, int fail_code)
{
    FILE *session_fp;
    FILE *src_fp;
    FILE *dest_fp;
    FILE *compare_fp;
    time_t rawtime;
    char dir[128];
    char fname[256];

    pthread_mutex_lock(&mg_log_mutex);
    time(&rawtime);

    sprintf(dir, "errlog_ctx%lu_%lu", ctx->id, rawtime);
    if(ctx->debug) {
        sprintf(dir, "debuglog_ctx%lu_%lu", ctx->id, rawtime);
    }

    MG_LOG_PRINT(g_log_fd, "Making directory [%s]\n", dir);
    if (mkdir(dir, 777) < 0) {
        MG_LOG_PRINT(g_log_fd, "Could not make directory %s\n", dir);
        return;
    }

    sprintf(fname, "%s/session_info.txt", dir);
    session_fp = fopen(fname, "w");

    switch (fail_code)
    {
        case DC_FAIL_CRC:
            fprintf(session_fp, "\n ******** Failure cause: CRC Error ********\n");
            break;
        case DC_FAIL_DATA:
            fprintf(session_fp, "\n ******** Failure cause: Data Compare ********\n");
            break;
        case DC_FAIL_SIZE:
            fprintf(session_fp, "\n ******** Failure cause: Size Mismatch ********\n");
            break;
        case DC_DEBUG:
            fprintf(session_fp, "\n ******** DEBUG: Captures all data on request ********\n");
            fprintf(session_fp, "\n ******** If you are doing this on 15K jobs, you probobly seg faulted.  ********\n");
            break;
        default:
            fprintf(session_fp, "\n ******** Unknown Cause Of Failure ********\n");
    }

    fprintf(session_fp, " ******** File [%s]\n", ctx->src_data->filename);
    fprintf(session_fp, " *         CompLevel:\t %u\n", ctx->sessCprSetupData.compLevel);
    fprintf(session_fp, " *          CompType:\t %s\n",
            ctx->sessCprSetupData.compType == CPA_DC_DEFLATE ? "DEFLATE" : "LZS");
    fprintf(session_fp, " *          HuffType:\t %s\n",
            ctx->sessCprSetupData.huffType == CPA_DC_HT_STATIC ? "STATIC" : "DYNAMIC");
    fprintf(session_fp, " *        AutoSelect:\t %u\n", ctx->sessCprSetupData.autoSelectBestHuffmanTree);
    fprintf(session_fp, " *     sessDirection:\t %u\n", ctx->sessCprSetupData.sessDirection);
    fprintf(session_fp, " *         sessState:\t %s\n",
            ctx->sessCprSetupData.sessState == CPA_DC_STATEFUL ? "STATEFUL" : "STATELESS");
    fprintf(session_fp, " * deflateWindowSize:\t %u\n", ctx->sessCprSetupData.windowSize);
    fprintf(session_fp, " *  Source File Size:\t %zu bytes\n", ctx->src_data->file_size);
    fprintf(session_fp, "\n\n");

    fprintf(session_fp, " ******** Context Details\n");
    fprintf(session_fp, " *     EOS type: %s\n", ctx->underflow ? "UNDERFLOW" : "OVERFLOW");
    if (ctx->underflow) {
        fprintf(session_fp, " * Underflow IBC: %u\n", ctx->uf_ibc);
    } else {
        fprintf(session_fp, " *  Overflow OBS: %u\n", ctx->obs);
    }
    fprintf(session_fp, "\n\n");

    fprintf(session_fp, " ******** Session Byte Count Details\n");

    if (!ctx->decomp_only) {
        fprintf(session_fp, "          Compression Results Status:\t %d \n", ctx->cpr_results.status );
        fprintf(session_fp, "   Last Compression Results Produced:\t %u bytes\n", ctx->cpr_results.produced );
        fprintf(session_fp, "   Last Compression Results Consumed:\t %u bytes\n", ctx->cpr_results.consumed );
        fprintf(session_fp, "  Total Compression Results Produced:\t %u bytes\n", ctx->cpr_produced );
        fprintf(session_fp, "  Total Compression Results Consumed:\t %u bytes\n", ctx->cpr_consumed );
        fprintf(session_fp, "        Compression Results Checksum:\t 0x%x\n", ctx->cpr_results.checksum );
        fprintf(session_fp, "  Compression Results endOfLastBlock:\t %u bytes\n", ctx->cpr_results.endOfLastBlock );
        fprintf(session_fp, "\n");
    }
    if (ctx->zlibcompare) {
        fprintf(session_fp, "                 Zlib Results Status:\t %d \n", ctx->zlib_results.status );
        fprintf(session_fp, "                  Zlib Results Size :\t %u bytes\n", ctx->zlib_results.size );
        fprintf(session_fp, "               Zlib Results Checksum:\t 0x%x\n", ctx->zlib_results.crc32 );
        fprintf(session_fp, "\n");
    }

    fprintf(session_fp, "        Decompression Results Status:\t %d \n", ctx->dcpr_results.status );
    fprintf(session_fp, " Last Decompression Results Produced:\t %u bytes\n", ctx->dcpr_results.produced );
    fprintf(session_fp, " Last Decompression Results Consumed:\t %u bytes\n", ctx->dcpr_results.consumed );
    fprintf(session_fp, "Total Decompression Results Produced:\t %u bytes\n", ctx->dcpr_produced );
    fprintf(session_fp, "Total Decompression Results Consumed:\t %u bytes\n", ctx->dcpr_consumed );
    fprintf(session_fp, "      Decompression Results Checksum:\t 0x%x\n", ctx->dcpr_results.checksum );
    fprintf(session_fp, "Decompression Results endOfLastBlock:\t %u\n", ctx->dcpr_results.endOfLastBlock );
    fprintf(session_fp, "\n");
    fprintf(session_fp, "\n");

    fprintf(session_fp, "Command to reproduce:\nmeatjet --threads=1 --comp-lvl=%u --infile=%s%s%s%s%u\n",
        ctx->sessCprSetupData.compLevel,
        ctx->src_data->filename,
        ctx->decomp_only ? " --decomp-only" : "",
        ctx->underflow ? " --underflow" : "",
        ctx->underflow ? " --ibc=" : " --obs=",
        ctx->underflow ? ctx->uf_ibc : ctx->obs);
    fclose(session_fp);

    // Write all buffers to file
    sprintf(fname, "%s/compare.bin", dir);
    compare_fp = fopen(fname, "w");
    fwrite(ctx->compare_mem, ctx->dcpr_produced, 1, compare_fp);
    fclose(compare_fp);

    sprintf(fname, "%s/source.bin", dir);
    src_fp = fopen(fname, "w");
    fwrite(ctx->src_data->src_mem, ctx->src_data->file_size, 1, src_fp);
    fclose(src_fp);

    sprintf(fname, "%s/dest.bin", dir);
    dest_fp = fopen(fname, "w");

    if (ctx->decomp_only) {
        fwrite(ctx->dest_mem, ctx->dcpr_produced, 1, dest_fp);
    } else {
        fwrite(ctx->dest_mem, ctx->cpr_produced, 1, dest_fp);
    }
    fclose(dest_fp);
    pthread_mutex_unlock(&mg_log_mutex);
}
