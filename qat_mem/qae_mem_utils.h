/* qae_mem_utils.h -- Support code for Intel QAT hardware acceleration of zlib.
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/*****************************************************************************
 * @file qae_mem_utils.h
 *
 * This file provides linux kernel memory allocation for quick assist API
 *
 *****************************************************************************/

#ifndef __QAE_MEM_UTILS_H
#define __QAE_MEM_UTILS_H

#include "cpa.h"

/*define types which need to vary between 32 and 64 bit*/
#ifdef __x86_64__
#define QAE_UINT  Cpa64U
#define QAE_INT   Cpa64S
#else
#define QAE_UINT  Cpa32U
#define QAE_INT  Cpa32S
#endif

#define QAE_BYTE_ALIGNMENT 0x0040 /* 64 bytes */

#define qaePinnedMemAlloc(m, n) qaeMemAlloc(m, n,__FILE__,__LINE__)
#define qaePinnedMemRealloc(m,s,n) qaeMemAlloc(m,s,n,__FILE__,__LINE__)

/*****************************************************************************
 * function:
 *         qaeMemAlloc(size_t memsize);
 *
 * @description
 *      allocates memsize bytes of memory aligned to a 64 byte address.
 *      The returned pointer will always be 64 byte aligned to ensure
 *      best performance from the accelerator.
 *
 * @param[in] memsize, the amount of memory in bytes to be allocated
 * @param[in] nodeId, the NUMA node Id to allocate on
 *
 * @retval pointer to the allocated memory
 *
 *****************************************************************************/
void *qaeMemAlloc(size_t memsize, Cpa32U nodeId, const char *file, int line);

/*****************************************************************************
 * function:
 *         qaeMemRealloc(void *ptr, size_t memsize)
 *
 * @description
 *      re-allocates memsize bytes of memory
 *
 * @param[in] pointer to existing memory
 * @param[in] memsize, the amount of memory in bytes to be allocated
 * @param[in] nodeId, the NUMA node Id to allocate on
 *
 * @retval pointer to the allocated memory
 *
 *****************************************************************************/
void *qaeMemRealloc(void *ptr, size_t memsize, Cpa32U nodeId, const char *file, int line);

/*****************************************************************************
 * function:
 *         qaePinnedMemFree(void *ptr)
 *
 * @description
 *      frees memory allocated by the qaeMemAlloc function
 *
 *
 * @param[in] pointer to the memory to be freed
 *
 * @retval none
 *
 *****************************************************************************/
void qaePinnedMemFree(void *ptr);


/*****************************************************************************
 * function:
 *         qaeMemV2P(void *v)
 *
 * @description
 * 	find the physical address of a block of memory referred to by virtual
 * 	address v in the current process's address map
 *
 *
 * @param[in] ptr, virtual pointer to the memory
 *
 * @retval the physical address of the memory referred to by ptr
 *
 *****************************************************************************/
CpaPhysicalAddr qaeMemV2P (void *v);

/* Placeholders to complete Interface */
CpaStatus qaeZlibMemInit(void);
void qaeZlibMemDestroy(void);
#endif
