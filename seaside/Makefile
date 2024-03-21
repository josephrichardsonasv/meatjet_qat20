ifndef PROJ
$(error "You must define PROJ environment variable --> COLETO_CREEK, CPMXX")
endif

ifndef BUILD_MG_FROM_SRC

	#
	# Build meatjet from package
	#

	ICP_SAMPLE_CODE ?= $(ICP_ROOT)/quickassist/lookaside/access_layer/src/sample_code/performance

	# Includes for compilation from package
	IDIR ?= -I$(ICP_ROOT)/quickassist/include
	IDIR += -I$(ICP_ROOT)/quickassist/include/dc
	IDIR += -I$(ICP_ROOT)/quickassist/lookaside/access_layer/include
	IDIR += -I$(ICP_ROOT)/quickassist/utilities/libusdm_drv/linux/include
	IDIR += -I/usr/include/python2.7
	IDIR += -I$(ICP_SAMPLE_CODE)/compression
	IDIR += -I$(ICP_SAMPLE_CODE)/framework
	IDIR += -I$(ICP_SAMPLE_CODE)/framework/linux/user_space
	IDIR += -I$(ICP_SAMPLE_CODE)/qae
	IDIR += -I$(ICP_ROOT)/quickassist/lookaside/access_layer/src/sample_code/busy_loop
    IDIR += -I$(ICP_ROOT)/quickassist/utilities/libusdm_drv/linux/include
        IDIR += -I$(ICP_ROOT)/quickassist/include/lac


	LDIR := $(ICP_ROOT)/build
        #LDIRC := $(ICP_ROOT)/quickassist/lookaside/access_layer/src/sample_code/build

	DEPS := chklib

else

	#
	# Build meatjet from source
	#

	QAT_SAMPLE_CODE ?= $(QAT)/isg_cid-qat_sample_code/sample_code/performance
	CMN_MEM_DRV_DIR ?= $(ICP_ROOT)/isg_cid-cmn_mem_driver
	SAL_DIR ?= $(ICP_ROOT)/isg_cid-qat_sal
	ICP_BUILDSYSTEM_PATH ?= $(ICP_ROOT)/isg_cid-build_system
	LAC_DIR ?= $(SAL_DIR)/me_acceleration_layer/access_layer/look_aside_acceleration
	SAMPLE_CODE_DIR ?= $(ICP_ROOT)/isg_cid-qat_sample_code/sample_code
	ICP_ENV_DIR ?= $(SAL_DIR)/me_acceleration_layer/release_files/tlm/env_files/

	# Includes for compilation from source
	IDIR += -I$(QAT)/isg_cid-qat_api/api
	IDIR += -I$(QAT)/isg_cid-qat_api/api/dc
	IDIR += -I$(QAT)/isg_cid-qat_api/api/lac/
	IDIR += -I$(QAT_SAMPLE_CODE)/qae
	IDIR += -I$(QAT_SAMPLE_CODE)/framework
	IDIR += -I$(QAT_SAMPLE_CODE)/compression
	IDIR += -I$(QAT_SAMPLE_CODE)/framework/linux/user_space
	IDIR += -I$(CMN_MEM_DRV_DIR)/linux/include
	IDIR += -I$(SAL_DIR)/me_acceleration_layer/access_layer/look_aside_acceleration/include

	# Requred for compilation from source, needs certain libs
	LDIR := ./lib

	DEPS := chklib cmn_mem adf osal lac

endif


LIBS := -lpthread -ldl -lutil -lm -Xlinker -export-dynamic -lz -ludev

CC = gcc
#CFLAGS = -Wall -Wextra -Werror -g -c -O0 -fms-extensions -fPIC -Wpointer-arith
CFLAGS = -g -c -O0 -fms-extensions -fPIC -Wpointer-arith

LDFLAGS = $(LDIR)/libusdm_drv.a $(LDIR)/libqat.a $(LDIR)/libadf.a $(LDIR)/libosal.a -lrt -lcrypto $(LDIR)/cpa_sample_code_s.so $(LIBS)

DEFINES = -DUSE_QAT_MEM -D_XOPEN_SOURCE=500 -DMG_UNIT_TEST -D_GNU_SOURCE -D$(PROJ) -DUSER_SPACE

ifeq ($(WITH_UPSTREAM), 1)
	DEFINES += -DWITH_UPSTREAM
endif

# OSAL Environment Requirements
OSAL_DIR ?= $(ICP_ROOT)/isg_cid-osal
OS_LEVEL ?= linux
TEAM_NAME ?= "DCG SV"

# Meatgrinder sources
SOURCES = main.c cpr.c buf_handler.c context.c mg_unit_test.c cpa_sample_code_dc_utils.c meatjet.c crc32.c
OBJECTS = $(SOURCES:.c=.o)
EXECUTABLE = meatjet

all: $(DEPS) $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(DEFINES) $(IDIR) $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $(DEFINES) $(IDIR) $< -o $@

.PHONY: chklib
chklib:
	@mkdir -p $(LDIR)

osal:
	@cd $(OSAL_DIR)/src; \
	$(MAKE) ICP_ENV_DIR=$(OSAL_DIR) lib
	@cp $(OSAL_DIR)/src/build/linux_2.6/user_space/libosal.a $(LDIR)

lac:
	@cd $(LAC_DIR)/src; \
	$(MAKE) ICP_ENV_DIR=$(ICP_ENV_DIR) ICP_BUILDSYSTEM_PATH=$(ICP_BUILDSYSTEM_PATH) ICP_OS_LEVEL=user_space lib_static
	@cp $(LAC_DIR)/src/build/linux_2.6/user_space/libqat.a $(LDIR)

cmn_mem:
	@cd $(CMN_MEM_DRV_DIR); \
	$(MAKE) ICP_ENV_DIR=$(ICP_ENV_DIR) ICP_BUILDSYSTEM_PATH=$(ICP_BUILDSYSTEM_PATH) cm_user
	@cp $(CMN_MEM_DRV_DIR)/libusdm_drv.a $(LDIR)

adf:
	@cd $(SAL_DIR)/linux/qat_direct/src; \
	$(MAKE) ICP_ENV_DIR=$(ICP_ENV_DIR) ICP_BUILDSYSTEM_PATH=$(ICP_BUILDSYSTEM_PATH)
	@cp $(SAL_DIR)/linux/qat_direct/src/build/linux_2.6/user_space/libadf.a $(LDIR)

.PHONY: clean
clean:
	/bin/rm -f *.o $(EXECUTABLE)

dbg-%:
	echo $*=$($*)
