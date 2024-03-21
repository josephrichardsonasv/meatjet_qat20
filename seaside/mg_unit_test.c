#include "mg_unit_test.h"

int ut_verify_src_sgl(CpaBufferList *sgl, char *filename)
{
    FILE *fd;
    uint32_t num_bufs;
    uint32_t buf_size;
    char *block;
    int ret = UT_PASS;

    fd = fopen(filename, "r");

    num_bufs = sgl->numBuffers;
    if (num_bufs <= 0) {
        printf("UT_FAIL - verify_src_sgl - num_bufs = %d\n", num_bufs);
        fclose(fd);
        return UT_FAIL;
    }

    // Assume that the first buffer size is the largest buffer
    buf_size = sgl->pBuffers[0].dataLenInBytes;

    block = (void *)malloc(buf_size);

    for (uint32_t i = 0; i < num_bufs; i++)
    {
        fread(block, 1, sgl->pBuffers[i].dataLenInBytes, fd);

        if (memcmp(block, sgl->pBuffers[i].pData, sgl->pBuffers[i].dataLenInBytes))
        {
            printf("UT_FAIL - verify_src_sgl - memcmp failed on block[%d]\n", i);
            fclose(fd);
            free(block);
            ret = UT_FAIL;
            break;
        }
    }


    free(block);
    fclose(fd);

    return ret;
}

int ut_mem_check_sgl()
{
    //
    return 0;
}
