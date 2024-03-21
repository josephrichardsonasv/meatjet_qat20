#pragma once

#include <stdbool.h>
#include <zlib.h>
#include "cpr.h"
#include "main.h"
#include "context.h"
#include "buf_handler.h"
#include "crc32.h"

#define DC_FAIL_CRC  0
#define DC_FAIL_DATA 1
#define DC_FAIL_SIZE 2
#define DC_DEBUG 3

CpaStatus meatjet(struct context *ctx, struct sgl_container *sgls);
void mg_log(struct context *ctx, int fail_code);
