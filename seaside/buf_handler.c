#include <string.h>
#include "buf_handler.h"
#include "main.h"
#include "qae_mem.h"

#ifdef DEBUG_CODE
extern Cpa32U g_alloc;
extern Cpa32U g_free;
extern pthread_mutex_t mem_mutex;
#endif

extern FILE *g_log_fd;

uint32_t calculate_num_buf(uint32_t file_size, uint32_t buf_size)
{
    uint32_t num_buf;

    num_buf = (file_size / buf_size);

    if (file_size % buf_size) {
        num_buf++;
    }

    return num_buf;
}

void mg_free_sgl(CpaBufferList *sgl)
{
    if (sgl == NULL) {
        MG_LOG_PRINT(g_log_fd, "Attempted to free SGL that doesn't exist\n");
        return;
    }

    for (uint32_t i = 0; i < sgl->numBuffers; i++)
    {
        if (sgl->pBuffers[i].pData != NULL) {
            qaeMemFreeNUMA((void **)&sgl->pBuffers[i].pData);
#ifdef DEBUG_CODE
            pthread_mutex_lock(&mem_mutex);
            g_free++;
            pthread_mutex_unlock(&mem_mutex);
#endif
        }
    }

    if (sgl->pPrivateMetaData != NULL) {
        qaeMemFreeNUMA((void **)&sgl->pPrivateMetaData);
#ifdef DEBUG_CODE
        pthread_mutex_lock(&mem_mutex);
        g_free++;
        pthread_mutex_unlock(&mem_mutex);
#endif
    }

    if (sgl->pBuffers != NULL) {
        qaeMemFreeNUMA((void **)&sgl->pBuffers);
#ifdef DEBUG_CODE
        pthread_mutex_lock(&mem_mutex);
        g_free++;
        pthread_mutex_unlock(&mem_mutex);
#endif
    }
}

CpaStatus mg_build_sgl(CpaBufferList *sgl, uint32_t node_id, uint32_t num_buf, uint32_t buf_size, uint32_t meta_size)
{
    CpaFlatBuffer *flat_buf;
    Cpa8U *p_data;

    if (num_buf == 0) {
        MG_LOG_PRINT(g_log_fd, "Error in mg_build_sgl: Cannot build SGL with 0 buffers\n");
        return CPA_STATUS_FAIL;
    }

    if (sgl == NULL) {
        MG_LOG_PRINT(g_log_fd, "Error in mg_build_sgl: SGL struct should already be allocataed\n");
        return CPA_STATUS_FAIL;
    }

    // Allocate Metadata
    if (meta_size) {
        sgl->pPrivateMetaData = (Cpa8U *) qaeMemAllocNUMA(meta_size, node_id, BYTE_ALIGNMENT_64);
        if (sgl->pPrivateMetaData == NULL) {
            MG_LOG_PRINT(g_log_fd, "Error in mg_build_sgl: Could not allocate meta size\n");
            return CPA_STATUS_FAIL;
        }

#ifdef DEBUG_CODE
        pthread_mutex_lock(&mem_mutex);
        g_alloc++;
        pthread_mutex_unlock(&mem_mutex);
#endif
    }

    // Allocate SGL Buffer Structs
    flat_buf = (CpaFlatBuffer *) qaeMemAllocNUMA(sizeof(CpaFlatBuffer) * num_buf, node_id, BYTE_ALIGNMENT_64);
    if (!flat_buf) {
        MG_LOG_PRINT(g_log_fd, "Error in mg_build_sgl: Could not allocate Flat Buffer List!\n");
        if (meta_size) {
            qaeMemFreeNUMA((void **)&sgl->pPrivateMetaData);
        }
        return CPA_STATUS_FAIL;
    }

#ifdef DEBUG_CODE
    pthread_mutex_lock(&mem_mutex);
    g_alloc++;
    pthread_mutex_unlock(&mem_mutex);
#endif

    sgl->pBuffers = flat_buf;

    // Allocate Data Buffers for each buffer in the list
    for (uint32_t i = 0; i < num_buf; i++)
    {
        p_data = (Cpa8U *) qaeMemAllocNUMA(buf_size, node_id, BYTE_ALIGNMENT_64);
        if (!p_data) {
            MG_LOG_PRINT(g_log_fd, "Error in mg_build_sgl: Could not allocate memory for p_data\n");
            sgl->numBuffers = i;
            mg_free_sgl(sgl);
            return CPA_STATUS_FAIL;
        }

#ifdef DEBUG_CODE
        pthread_mutex_lock(&mem_mutex);
        g_alloc++;
        pthread_mutex_unlock(&mem_mutex);
#endif

        flat_buf->pData = p_data;
        flat_buf->dataLenInBytes = buf_size;
        flat_buf++;
    }

    sgl->numBuffers = num_buf;

    return CPA_STATUS_SUCCESS;
}

/*
    Function:

        copy_mem_to_sgl

    Copies memory from a buffer in memory into an SGL

    Notes:

        - Assumes we are starting at the first buffer in SGL, and will overwrite all data

    Parameters:

        data_ptr    - Pointer to source memory buffer
        sgl         - Pointer to dest SGL
        sgl_buf_sz  - Size of the buffers in the SGL
        copy_amt    - Total amount to be copied into SGL

    Returns:

        Returns the difference between orig_data ptr and final data_ptr, indicating the amt copied
*/

Cpa32U copy_mem_to_sgl(Cpa8U *data_ptr, CpaBufferList *sgl, Cpa32U sgl_buf_sz, Cpa32U copy_amt)
{
    Cpa32U i;
    Cpa32U copy_val;
    Cpa32U amt_remaining;
    CpaFlatBuffer *cur_buf;
    Cpa8U *orig_data_ptr;

    if (data_ptr == NULL) {
        MG_LOG_PRINT(g_log_fd, "Data pointer is NULL\n");
        return 0;
    }

    if (sgl == NULL) {
        MG_LOG_PRINT(g_log_fd, "SGL pointer is NULL\n");
        return 0;
    }

    orig_data_ptr = data_ptr;
    cur_buf = sgl->pBuffers;    // point to the first SGL buffer
    amt_remaining = copy_amt;
    copy_val = sgl_buf_sz;      // set initial copy size to the entire buffer

    i = 0;

    while (amt_remaining > 0)
    {
        if (amt_remaining < sgl_buf_sz) {
            copy_val = amt_remaining;
        }

        memcpy(cur_buf->pData, data_ptr, copy_val);

        amt_remaining -= copy_val;
        data_ptr += copy_val;

        cur_buf->dataLenInBytes = copy_val;
        cur_buf++;
        i++;
    }

    // Clear out dataLenInBytes for the remaining flat buffers
    for (; i < sgl->numBuffers; i++)
    {
        sgl->pBuffers[i].dataLenInBytes = 0;
    }

    return (Cpa32U)(data_ptr - orig_data_ptr);
}

/*
    Function:

        copy_sgl_to_mem

    Copies an entire SGL to a location in memory

    Notes:

        - Assumes it's always the entire SGL that will be copied, cannot be a portion of an SGL

    Parameters:

        sgl         - Pointer to source SGL
        data_ptr    - Pointer to dest memory buffer
        sgl_buf_sz  - Size of each buffer in the SGL
        copy_amt    - Total amount of data within the SGL to copy

    Returns:

        Returns the difference between orig_data ptr and final data_ptr, indicating the amt copied
*/
Cpa32U copy_sgl_to_mem(CpaBufferList *sgl, Cpa8U *data_ptr, Cpa32U sgl_buf_sz, Cpa32U size)
{
    Cpa32U i;
    Cpa32U total_copied = 0;
    Cpa32U copy_request;

    if (sgl == NULL) {
        MG_LOG_PRINT(g_log_fd, "Failed to write SGL to file, NULL ptr\n");
        return 0;
    }

    if (sgl_buf_sz == 0) {
        MG_LOG_PRINT(g_log_fd, "Buffer size cannot be zero\n");
        return 0;
    }

    for (i = 0; i < sgl->numBuffers; i++)
    {
        if (sgl->pBuffers[i].dataLenInBytes + total_copied > size) {
            copy_request = size - total_copied;
        } else {
            copy_request = sgl->pBuffers[i].dataLenInBytes;
        }

        data_ptr = mempcpy(data_ptr, sgl->pBuffers[i].pData, copy_request);
        total_copied += copy_request;

    }

    return total_copied;
}

/*
    Function:

        write_sgl_to_file

    Writes a complete SGL to a file on disk

    Parameters:

        sgl         - Pointer to the SGL that should be written
        filename    - Filename where SGL will be located

    Returns:

        1 if failure occurs, otherwise MGSUCCESS
*/
int write_sgl_to_file(CpaBufferList *sgl, Cpa32U size, char *filename)
{
    Cpa32U i;
    size_t bytes;
    FILE *fd;
    Cpa32U total_copied;
    Cpa32U copy_request;

    if (sgl == NULL) {
        MG_LOG_PRINT(g_log_fd, "Failed to write SGL to file, NULL ptr\n");
        return -1;
    }

    fd = fopen(filename, "w");
    if (fd == NULL) {
        MG_LOG_PRINT(g_log_fd, "Failed to open file %s\n", filename);
        return -1;
    }

    total_copied = 0;
    for (i = 0; i < sgl->numBuffers; i++)
    {
        if (sgl->pBuffers[i].dataLenInBytes + total_copied > size) {
            copy_request = size - total_copied;
        } else {
            copy_request = sgl->pBuffers[i].dataLenInBytes;
        }

        bytes = fwrite(sgl->pBuffers[i].pData, 1, copy_request, fd);
        if (bytes != copy_request) {
            MG_LOG_PRINT(g_log_fd, "Failed to write the correct number of bytes to the output file!\n");
            return -1;
        }

        total_copied += bytes;
    }

    fclose(fd);

    return 0;
}
