#pragma once

#include <pthread.h>
#include "cpa.h"
#include "cpa_types.h"
#include "cpa_dc.h"

CpaStatus mg_build_sgl(CpaBufferList *sgl, uint32_t nod_id, uint32_t num_buf, uint32_t buf_size, uint32_t meta_size);
void mg_free_sgl(CpaBufferList *sgl);
Cpa32U copy_mem_to_sgl(Cpa8U *data_ptr, CpaBufferList *sgl, Cpa32U sgl_buf_sz, Cpa32U copy_amt);
Cpa32U copy_sgl_to_mem(CpaBufferList *sgl, Cpa8U *data_ptr, Cpa32U sgl_buf_sz, Cpa32U size);
int write_sgl_to_file(CpaBufferList *sgl, Cpa32U size, char *filename);
