/***************************************************************************
 *
 * <COPYRIGHT_TAG>
 *
 ***************************************************************************/
/**
 *****************************************************************************
 * @file qae_mem_utils.c
 *
 * This file provides provide for Linux user space memory allocation. It uses
 * a driver that allocates the memory in kernel memory space (to ensure
 * physically contiguous memory) and maps it to
 * user space for use by the  quick assist sample code
 *
 *****************************************************************************/

#include "qae_mem_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>
#ifdef SAL_IOMMU_CODE
#include <icp_sal_iommu.h>
#endif

#define QAE_MEM "/dev/qae_mem"
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE-1))
#define USER_MEM_128BYTE_OFFSET         (128)

static pthread_mutex_t mutex_g = PTHREAD_MUTEX_INITIALIZER;
static dev_mem_info_t *pUserMemList = NULL;
static dev_mem_info_t *pUserMemListHead = NULL;

static int fd = 0;
extern FILE *g_log_fd;

#ifdef DEBUG_CODE
Cpa32U numaAllocations_g = 0;
Cpa32U normalAllocations_g = 0;
#endif


/**************************************
 * Memory functions
 *************************************/
CpaStatus qaeMemInit(void)
{
    fd = open(QAE_MEM, O_RDWR);
    if (fd < 0)
    {
        MG_LOG_PRINT(g_log_fd, "unable to open %s %d\n",QAE_MEM,fd);
        return CPA_STATUS_FAIL;
    }
    return CPA_STATUS_SUCCESS;
}

void qaeMemDestroy(void)
{
    close(fd);
}


void * qaeMemAlloc (Cpa32U memsize)
{
#ifdef DEBUG_CODE
    normalAllocations_g++;
#endif
    QAE_UINT *memPtr = NULL;
    memPtr = calloc(1,memsize);
#ifdef DEBUG_CODE
    if(!memPtr)
    {
        normalAllocations_g--;
    }
#endif
    return memPtr;
}

static CpaStatus userMemListAdd(dev_mem_info_t *pMemInfo)
{
    int ret = 0;
    ret = pthread_mutex_lock(&mutex_g);
    if(0 != ret)
    {
        MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex lock\n", ret);
        return CPA_STATUS_FAIL;
    }
    ADD_ELEMENT_TO_END_OF_LIST(pMemInfo, pUserMemList, pUserMemListHead);
    ret = pthread_mutex_unlock(&mutex_g);
    if(0 != ret)
    {
        MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex unlock\n", ret);
        return CPA_STATUS_FAIL;
    }
    return CPA_STATUS_SUCCESS;
}

static void userMemListFree(dev_mem_info_t *pMemInfo)
{
    dev_mem_info_t *pCurr = NULL;

    for (pCurr = pUserMemListHead; pCurr != NULL; pCurr = pCurr->pNext)
    {
        if (pCurr == pMemInfo)
        {
            REMOVE_ELEMENT_FROM_LIST(pCurr, pUserMemList, pUserMemListHead);
            break;
        }
    }
}


static dev_mem_info_t* userMemLookupBySize(Cpa32U size)
{
    dev_mem_info_t *pCurr = NULL;

    for (pCurr = pUserMemListHead; pCurr != NULL; pCurr = pCurr->pNext)
    {
        if (pCurr->available_size >= size)
        {
            return pCurr;
        }
    }
    return NULL;
}

static dev_mem_info_t* userMemLookupByVirtAddr(void* virt_addr)
{
    dev_mem_info_t *pCurr = NULL;

    for (pCurr = pUserMemListHead; pCurr != NULL; pCurr = pCurr->pNext)
    {
        if ((QAE_UINT)pCurr->virt_addr <= (QAE_UINT)virt_addr &&
                ((QAE_UINT)pCurr->virt_addr + pCurr->size) > (QAE_UINT)virt_addr)
        {
            return pCurr;
        }
    }
    return NULL;
}


void * qaeMemAllocNUMA (Cpa32U size, Cpa32U node, Cpa32U alignment)
{
    int ret = 0;
    dev_mem_info_t *pMemInfo = NULL;
    void* pVirtAddress = NULL;
    void* pOriginalAddress = NULL;
    QAE_UINT padding = 0;
    QAE_UINT aligned_address = 0;

    if (size == 0 || alignment == 0)
    {
        MG_LOG_PRINT(g_log_fd, "Invalid size or alignment parameter\n");
        return NULL;
    }
    if (fd < 0)
    {
        MG_LOG_PRINT(g_log_fd, "Memory file handle is not ready\n");
        return NULL;
    }

    ret = pthread_mutex_lock(&mutex_g);
    if(0 != ret)
    {
        MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex lock\n", ret);
        return NULL;
    }

    if ( (pMemInfo = userMemLookupBySize(size + alignment)) != NULL)
    {
        pOriginalAddress = (void*) ((QAE_UINT) pMemInfo->virt_addr +
                (QAE_UINT)(pMemInfo->size - pMemInfo->available_size));
        padding = (QAE_UINT) pOriginalAddress % alignment;
        aligned_address = ((QAE_UINT) pOriginalAddress) - padding
        + alignment;
        pMemInfo->available_size -= (size + (aligned_address -
                (QAE_UINT) pOriginalAddress));
        pMemInfo->allocations += 1;
        ret = pthread_mutex_unlock(&mutex_g);
        if(0 != ret)
        {
            MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex lock\n", ret);
            return NULL;
        }
        return (void*) aligned_address;
    }
    ret = pthread_mutex_unlock(&mutex_g);
    if(0 != ret)
    {
        MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex lock\n", ret);
        return NULL;
    }

    pMemInfo = calloc(1,sizeof(dev_mem_info_t));
    if (NULL == pMemInfo)
    {
        MG_LOG_PRINT(g_log_fd, "unable to allocate pMemInfo buffer\n");
        return NULL;
    }

    pMemInfo->allocations = 0;

    pMemInfo->size = USER_MEM_128BYTE_OFFSET + size;
    pMemInfo->size = pMemInfo->size%PAGE_SIZE?
            ((pMemInfo->size/PAGE_SIZE)+1)*PAGE_SIZE:
            pMemInfo->size;
#ifdef SAL_IOMMU_CODE
    pMemInfo->size = icp_sal_iommu_get_remap_size(pMemInfo->size);
#endif
    pMemInfo->nodeId = node;
    ret = ioctl(fd, DEV_MEM_IOC_MEMALLOC, pMemInfo);
    if (ret != 0)
    {
        MG_LOG_PRINT(g_log_fd, "ioctl call failed, ret = %d\n",ret);
        free(pMemInfo);
        return NULL;
    }

    pMemInfo->virt_addr = mmap((void *) 0, pMemInfo->size,
            PROT_READ|PROT_WRITE, MAP_SHARED, fd,
            (pMemInfo->id * getpagesize()));

    if (pMemInfo->virt_addr == (void *) MAP_FAILED)
    {
        MG_LOG_PRINT(g_log_fd, "mmap failed\n");
        ret = ioctl(fd, DEV_MEM_IOC_MEMFREE, pMemInfo);
        if (ret != 0)
        {
            MG_LOG_PRINT(g_log_fd, "ioctl call failed, ret = %d\n",ret);
        }
        free(pMemInfo);
        return NULL;
    }
#ifdef SAL_IOMMU_CODE
    if (icp_sal_iommu_map(pMemInfo->phy_addr, pMemInfo->phy_addr, pMemInfo->size))
    {
        if (munmap(pMemInfo->virt_addr, pMemInfo->size) != 0)
        {
            MG_LOG_PRINT(g_log_fd, "munmap failed\n");
        }
        if (ioctl(fd, DEV_MEM_IOC_MEMFREE, pMemInfo) != 0)
        {
            MG_LOG_PRINT(g_log_fd, "ioctl call failed\n");
        }
        free(pMemInfo);
        MG_LOG_PRINT(g_log_fd, "iommu map call failed\n");
    }
#endif
    pMemInfo->available_size = pMemInfo->size - size - USER_MEM_128BYTE_OFFSET;
    pMemInfo->allocations = 1;
    memcpy(pMemInfo->virt_addr, pMemInfo, sizeof(dev_mem_info_t));
    pVirtAddress = (void *)((QAE_UINT)pMemInfo->virt_addr
            + USER_MEM_128BYTE_OFFSET);
    if(CPA_STATUS_SUCCESS != userMemListAdd(pMemInfo))
    {
        MG_LOG_PRINT(g_log_fd, "Error on mem list add\n");
        return NULL;
    }
#ifdef DEBUG_CODE
    numaAllocations_g++;
#endif
    return pVirtAddress;
}

void qaeMemFreeNUMA (void** ptr)
{
    int ret = 0;
    dev_mem_info_t *pMemInfo = NULL;
    void* pVirtAddress = NULL;


    if (NULL == ptr)
    {
        MG_LOG_PRINT(g_log_fd, "Invalid virtual address\n");
        return;
    }
    pVirtAddress = *ptr;
    if(pVirtAddress == NULL)
    {
        MG_LOG_PRINT(g_log_fd, "Invalid virtual address\n");
        return;
    }
    ret = pthread_mutex_lock(&mutex_g);
    if(0 != ret)
    {
        MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex lock\n", ret);
        return;
    }
    if ((pMemInfo = userMemLookupByVirtAddr(pVirtAddress)) != NULL)
    {
        pMemInfo->allocations -= 1;
        if (pMemInfo->allocations != 0)
        {
            *ptr = NULL;
            ret = pthread_mutex_unlock(&mutex_g);
            if(0 != ret)
            {
                MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex unlock\n", ret);
                return;
            }
            return;
        }
    }
    else
    {
        MG_LOG_PRINT(g_log_fd, "userMemLookupByVirtAddr failed\n");
        ret = pthread_mutex_unlock(&mutex_g);
        if(0 != ret)
        {
            MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex unlock\n", ret);
            return;
        }
        return;
    }

    ret = munmap(pMemInfo->virt_addr, pMemInfo->size);
    if (ret != 0)
    {
        MG_LOG_PRINT(g_log_fd, "munmap failed, ret = %d\n",ret);
    }

    ret = ioctl(fd, DEV_MEM_IOC_MEMFREE, pMemInfo);
    if (ret != 0)
    {
        MG_LOG_PRINT(g_log_fd, "ioctl call failed, ret = %d\n",ret);
    }
#ifdef SAL_IOMMU_CODE
    if (icp_sal_iommu_unmap(pMemInfo->phy_addr, pMemInfo->size))
    {
        MG_LOG_PRINT(g_log_fd, "iommu unmap call failed\n");
    }
#endif
    userMemListFree(pMemInfo);
    free(pMemInfo);
    *ptr = NULL;
    ret = pthread_mutex_unlock(&mutex_g);
    if(0 != ret)
    {
        MG_LOG_PRINT(g_log_fd, "Error(%d) on thread mutex lock\n", ret);
        return;
    }
#ifdef DEBUG_CODE
    numaAllocations_g--;
#endif
    return;
}


void qaeMemFree (void **ptr)
{
    if (NULL == ptr || NULL == *ptr)
    {
        MG_LOG_PRINT(g_log_fd, "ERROR, Trying to Free NULL Pointer\n");
        return;
    }
    free(*ptr);
#ifdef DEBUG_CODE
    normalAllocations_g--;
#endif
    *ptr = NULL;
}

QAE_PHYS_ADDR qaeVirtToPhysNUMA(void* pVirtAddress)
{
    dev_mem_info_t *pMemInfo = NULL;
    void *pVirtPageAddress = NULL;
    QAE_UINT offset = 0;
    if(pVirtAddress==NULL)
    {
        MG_LOG_PRINT(g_log_fd, "qaeVirtToPhysNUMA():   Null virtual address pointer\n");
        return (QAE_PHYS_ADDR) 0;
    }

    pVirtPageAddress = ((int *)((((QAE_UINT)pVirtAddress)) & (PAGE_MASK)));

    offset = (QAE_UINT)pVirtAddress - (QAE_UINT)pVirtPageAddress;

    do
    {
        pMemInfo = (dev_mem_info_t *)pVirtPageAddress;
        if (pMemInfo->virt_addr == pVirtPageAddress)
        {
            break;
        }
        pVirtPageAddress = (void*)((QAE_UINT)pVirtPageAddress - PAGE_SIZE);

        offset += PAGE_SIZE;
    }
    while (pMemInfo->virt_addr != pVirtPageAddress);
    return (QAE_PHYS_ADDR)(pMemInfo->phy_addr + offset);
}

#ifdef DEBUG_CODE
void printMemAllocations()
{
    MG_LOG_PRINT(g_log_fd, "NUMA Allocations %d\n", numaAllocations_g);
    MG_LOG_PRINT(g_log_fd, "Normal Allocations %d\n", normalAllocations_g);
}
#endif




