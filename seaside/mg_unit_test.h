#ifndef __MG_UNIT_TEST_H__
#define __MG_UNIT_TEST_H__

#include <string.h>

#include "cpa.h"
#include "cpa_types.h"
#include "cpa_dc.h"
#include "buf_handler.h"
#include "main.h"

#define UT_PASS 0
#define UT_FAIL 1

//
int ut_verify_src_sgl(CpaBufferList *sgl, char *filename);

#endif
