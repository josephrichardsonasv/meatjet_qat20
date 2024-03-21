/***************************************************************************
 *
 * <COPYRIGHT_TAG>
 *
 ***************************************************************************/

/**
 *****************************************************************************
 * @file cpa_sample_code_dc_utils.c
 *
 * @defgroup compressionThreads
 *
 * @ingroup compressionThreads
 *
 * @description
 * Contains function prototypes and #defines used throughout code
 * and macros
 *
 ***************************************************************************/

#include "cpa_sample_code_utils_common.h"
#include "cpa_sample_code_dc_perf.h"
#include "cpa_sample_code_framework.h"
#include "cpa_sample_code_dc_utils.h"
#include <sys/utsname.h>
#include <time.h>
#include <asm/param.h>
#include <sched.h>
#include <semaphore.h>

#include "icp_sal_poll.h"

#ifdef SAMPLE_CODE_WAIT_DEFAULT
#define SAMPLE_CODE_WAIT_THIRTY_SEC SAMPLE_CODE_WAIT_DEFAULT
#endif


#ifdef DEBUG_CODE
/* Global Counter for Memory Allocations used for Debug Only */
Cpa32U allocate_g = 0;
/* Global Counter for Memory Frees used for Debug Only*/
Cpa32U free_g = 0;
#endif

Cpa32U expansionFactor_g = 1;
#ifdef ZERO_BYTE_LAST_REQUEST
CpaBoolean zeroByteLastRequest_g = CPA_FALSE;
#endif
extern int signOfLife;
/* Global array of polling threads */
sample_code_thread_t* dcPollingThread_g = NULL;

/* Number of Compression instances enabled for polling */
Cpa32U numDcPolledInstances_g = 0;

/* Global array of instance handles */
CpaInstanceHandle *dcInstances_g = NULL;

/* Number of Compression instances available */
Cpa16U numDcInstances_g = 0;

/* Flag to indicate if the DC services are started */
CpaBoolean dc_service_started_g = CPA_FALSE;

/* Flag to indicate if the DC polling threads have been created */
volatile CpaBoolean dc_polling_started_g = CPA_FALSE;

/* flag to define weather to use zlib to compress data before decompression*/
CpaBoolean useZlib_g = CPA_FALSE;

/* Dynamic Buffer List buffer list used to start DC Services */
CpaBufferList ***pInterBuffList_g = NULL;

Cpa32U g_coreLimit = 1;

#define __DCCPUID(in,a,b,c,d)   \
    __asm volatile ("cpuid": "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (in));

__inline__ static void sampleCodeCpuid(void)
{
    unsigned int a = 0x00, b = 0x00, c= 0x00, d = 0x00;
    __DCCPUID(0x00, a, b, c, d);
}

__inline__ static Cpa64U sampleCodeRdtscp(void)
{
    volatile unsigned long a = 0, d = 0;
    Cpa64U returnval = 0;

    sampleCodeCpuid();
    __asm volatile ("rdtsc" : "=a" (a), "=d" (d));
    returnval = (((Cpa64U)d) << UPPER_HALF_OF_REGISTER);
    returnval |= ((Cpa64U)a);

    return returnval;
}


static perf_cycles_t dc_getTimestamp(void)
{
    /*get time stamp twice, because we need to prime the timestamp counter*/
    sampleCodeRdtscp();
    return (perf_cycles_t)sampleCodeRdtscp();
}


#define SINGLE_INTER_BUFF_LIST        (1)

CpaStatus sampleCodeTimeTGet (sample_code_time_t* ptime)
{
    struct timeval tval;

    if (gettimeofday(&tval,NULL) == -1)
    {
        PRINT_ERR("sampleCodeTimeTGet(): gettimeofday system call failed \n");

        return CPA_STATUS_FAIL;
    }
    ptime->secs = tval.tv_sec;
    /*
     * gettimeofday returns in terms of sec and uSec.
     * Convert it into sec and nanoseconds into sample_code_time_t type
     */
    ptime->nsecs = tval.tv_usec * 1000;

    return CPA_STATUS_SUCCESS;
}

CpaStatus sampleCodeSemaphoreWait(sample_code_semaphore_t * semPtr, Cpa32S timeout)
{
    Cpa32S status;
    sample_code_time_t timeoutVal,currTime;

    CHECK_POINTER_AND_RETURN_FAIL_IF_NULL(semPtr);

    /*
     * Guard against illegal timeout values
     * WAIT_FORVER = -1
     */
    if (timeout < SAMPLE_CODE_WAIT_FOREVER)
    {
        PRINT_ERR("sample_code_semaphoreWait(): illegal timeout value \n");
        return CPA_STATUS_FAIL;
    }

    if (timeout == SAMPLE_CODE_WAIT_FOREVER)
    {
        status = sem_wait(*semPtr);
    }
    else if (timeout == SAMPLE_CODE_WAIT_NONE)
    {
        status = sem_trywait(*semPtr);
    }
    else
    {
        /*
        * Convert the inputted time into appropriate timespec struct.
        * Since timespec and sample_code_time_t timeval are of the the same
        * type.
        * Reuse the timeval to timespec macros.
        */
        SAMPLE_CODE_TIMEVAL_TO_MS(timeout,&timeoutVal);

        /* Get current time */
        if(CPA_STATUS_SUCCESS != sampleCodeTimeTGet(&currTime))
        {
            return CPA_STATUS_FAIL;
        }
        /* Add this to the timeout so that it gives absolute timeout value */
        SAMPLE_CODE_TIME_ADD(timeoutVal, currTime);

        /*this loop waits until the timeout, when timeout occurs sem_timedwait
         * returns -1 with errno = ETIMEDOUT, EINTR means that some signal
         * caused a premature exit of sem_timedwait and it loops again for the
         * specified timeout*/
        status = sem_wait(*semPtr);
    }

    if (status < 0)
    {
        PRINT_ERR("sample_code_semaphoreWait(): errno: %d \n", errno);
        return CPA_STATUS_FAIL;
    }
    else
    {
        return CPA_STATUS_SUCCESS;
    }
}

CpaStatus dc_threadCreate
(
        sample_code_thread_t *thread,
        sample_code_thread_attr_t *threadAttr,
        performance_func_t function,
        void *params)
{
    //CHECK_POINTER_AND_RETURN_FAIL_IF_NULL(thread);
    //CHECK_POINTER_AND_RETURN_FAIL_IF_NULL(function);

    int status = 1;
    pthread_attr_t attr;
    struct sched_param param;


    status = pthread_attr_init(&attr);
    if(status !=0)
    {
        PRINT_ERR("%d\n", errno);
        return CPA_STATUS_FAIL;
    }


    /* Setting scheduling parameter will fail for non root user,
     * as the default value of inheritsched is PTHREAD_EXPLICIT_SCHED in
     * POSIX. It is not required to set it explicitly before setting the
     * scheduling policy */


    if(threadAttr == NULL)
    {
        status = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        if(status !=0)
        {
            pthread_attr_destroy(&attr);
            PRINT_ERR("%d\n", errno);
            return CPA_STATUS_FAIL;
        }



        status = pthread_attr_setschedpolicy(&attr,
                THREAD_DEFAULT_SCHED_POLICY);
        if(status !=0)
        {
            pthread_attr_destroy(&attr);
            PRINT_ERR("%d\n", errno);
            return CPA_STATUS_FAIL;
        }

        /* Set priority based on value in threadAttr */
        memset(&param, 0, sizeof(param));
        param.sched_priority = THREAD_PRIORITY_SCHED_OTHER ;

        status = pthread_attr_setschedparam(&attr, &param);
        if(status !=0)
        {
            pthread_attr_destroy(&attr);
            PRINT_ERR("%d\n", errno);
            return CPA_STATUS_FAIL;
        }


    }
    else
    {
        /* Set scheduling policy based on value in threadAttr */

        if((threadAttr->policy != SCHED_RR) &&
                (threadAttr->policy != SCHED_FIFO) &&
                (threadAttr->policy != SCHED_OTHER))
        {
            threadAttr->policy = THREAD_DEFAULT_SCHED_POLICY;
        }

        status = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        if(status !=0)
        {
            pthread_attr_destroy(&attr);
            PRINT_ERR("%d\n", errno);
            return CPA_STATUS_FAIL;
        }



        status = pthread_attr_setschedpolicy(&attr, threadAttr->policy);
        if(status !=0)
        {
            pthread_attr_destroy(&attr);
            PRINT_ERR("%d\n", errno);
            return CPA_STATUS_FAIL;
        }


        /* Set priority based on value in threadAttr */
        memset(&param, 0, sizeof(param));
        param.sched_priority = threadAttr->priority;

        if(threadAttr->policy != SCHED_OTHER)
        {
            status =pthread_attr_setschedparam(&attr, &param);
            if(status !=0)
            {
                pthread_attr_destroy(&attr);
                PRINT_ERR("%d\n", errno);
                return CPA_STATUS_FAIL;
            }

        }
    }

    status = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    if(status !=0)
    {
        pthread_attr_destroy(&attr);
        PRINT_ERR("%d\n", errno);
        return CPA_STATUS_FAIL;
    }


    status = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    if(status !=0)
    {
        pthread_attr_destroy(&attr);
        PRINT_ERR("%d\n", errno);
        return CPA_STATUS_FAIL;
    }

    /*pthread_create expects "void *(*start_routine)(void*)" as the 3rd argument
     * but we are calling functions with return void instead of void*, normally
     * the return value of the start_routine contains the exit status, in this
     * sample code we track any internal errors in the start_routine, to allow
     * this to compile we need to cast "function" parameter, this is the same
     * as calling it as  void *(*function)(void*)*/
    status = pthread_create(thread, &attr,(void * (*)(void *))function, params);
    if(status !=0)
    {
        pthread_attr_destroy(&attr);
        PRINT_ERR("%d\n", errno);
        return CPA_STATUS_FAIL;
    }
    /*destroy the thread attributes as they are no longer required, this does
    * not affect the created thread*/
    pthread_attr_destroy(&attr);
    return CPA_STATUS_SUCCESS;


}

/*
CpaStatus sampleCodeThreadBind (sample_code_thread_t *thread, Cpa32U logicalCore)
{
    int status = 1;
    cpu_set_t cpuset;
    CHECK_POINTER_AND_RETURN_FAIL_IF_NULL(thread);
    CPU_ZERO(&cpuset);
    CPU_SET(logicalCore, &cpuset);

    status = pthread_setaffinity_np(*thread, sizeof(cpu_set_t), &cpuset);
    if(status !=0)
    {
        PRINT_ERR("sampleCodeThreadBind, %d\n", status);
        PRINT_ERR("sampleCodeThreadBind, %d", errno);
        return CPA_STATUS_FAIL;
    }
    return CPA_STATUS_SUCCESS;

}
*/

static void freeDcBufferList(CpaBufferList **buffListArray,
               Cpa32U numberOfBufferList)
{
    uint32_t i = 0, j = 0;
    Cpa32U numberOfBuffers=0;

#ifdef DEBUG_CODE
    PRINT("***********************************\n");
    PRINT("Entered Function:: %s\n",__FUNCTION__);
#endif
    i = numberOfBufferList;
    for (i = 0; i < numberOfBufferList; i++)
    {
      numberOfBuffers = buffListArray[i]->numBuffers;
      for(j=0; j< numberOfBuffers; j++)
      {
          if(buffListArray[i]->pBuffers[j].pData != NULL)
          {
              qaeMemFreeNUMA((void**)&buffListArray[i]->pBuffers[j].pData);
              buffListArray[i]->pBuffers[j].pData = NULL;
  #ifdef DEBUG_CODE
              free_g++;
  #endif
          }
      }
      if(buffListArray[i]->pBuffers !=NULL)
      {

          qaeMemFreeNUMA((void**)&buffListArray[i]->pBuffers);
  #ifdef DEBUG_CODE
          free_g++;
  #endif
          buffListArray[i]->pBuffers = NULL;
      }

      if(buffListArray[i]->pPrivateMetaData !=NULL)
      {

          qaeMemFreeNUMA((void**)&buffListArray[i]->pPrivateMetaData);
  #ifdef DEBUG_CODE
          free_g++;
  #endif
      }
    }
#ifdef DEBUG_CODE
    PRINT("Exit From Function:: %s\n",__FUNCTION__);
    PRINT("***********************************\n");
#endif
}


#ifdef STV_TEST_CODE
static char *zlibFiles [] =
    {
"qat/block0.dat", "qat/block100.dat", "qat/block101.dat", "qat/block102.dat",
"qat/block103.dat", "qat/block104.dat",
"qat/block105.dat", "qat/block106.dat", "qat/block107.dat", "qat/block108.dat",
"qat/block10.dat", "qat/block110.dat", "qat/block111.dat", "qat/block112.dat",
"qat/block113.dat", "qat/block114.dat", "qat/block115.dat", "qat/block116.dat",
"qat/block117.dat", "qat/block118.dat", "qat/block119.dat", "qat/block11.dat",
"qat/block120.dat", "qat/block121.dat", "qat/block122.dat", "qat/block123.dat",
"qat/block124.dat", "qat/block125.dat", "qat/block126.dat", "qat/block127.dat",
"qat/block128.dat", "qat/block129.dat", "qat/block12.dat", "qat/block130.dat",
"qat/block131.dat", "qat/block132.dat", "qat/block133.dat", "qat/block134.dat",
"qat/block135.dat", "qat/block136.dat", "qat/block137.dat", "qat/block138.dat",
"qat/block139.dat", "qat/block13.dat", "qat/block140.dat", "qat/block141.dat",
"qat/block142.dat", "qat/block143.dat", "qat/block144.dat", "qat/block145.dat",
"qat/block146.dat", "qat/block147.dat", "qat/block148.dat", "qat/block149.dat",
"qat/block14.dat", "qat/block150.dat", "qat/block151.dat", "qat/block152.dat",
"qat/block153.dat", "qat/block154.dat", "qat/block155.dat", "qat/block156.dat",
"qat/block157.dat", "qat/block158.dat", "qat/block159.dat", "qat/block15.dat",
"qat/block160.dat", "qat/block161.dat", "qat/block162.dat", "qat/block163.dat",
"qat/block164.dat", "qat/block165.dat", "qat/block166.dat", "qat/block167.dat",
"qat/block168.dat", "qat/block169.dat", "qat/block16.dat", "qat/block170.dat",
"qat/block171.dat", "qat/block172.dat", "qat/block173.dat", "qat/block174.dat",
"qat/block175.dat", "qat/block176.dat", "qat/block177.dat", "qat/block178.dat",
"qat/block179.dat", "qat/block17.dat", "qat/block180.dat", "qat/block181.dat",
"qat/block182.dat", "qat/block183.dat", "qat/block184.dat", "qat/block185.dat",
"qat/block186.dat", "qat/block187.dat", "qat/block188.dat", "qat/block189.dat",
"qat/block18.dat", "qat/block190.dat", "qat/block191.dat", "qat/block192.dat",
"qat/block193.dat", "qat/block194.dat", "qat/block195.dat", "qat/block196.dat",
"qat/block197.dat", "qat/block198.dat", "qat/block199.dat", "qat/block19.dat",
"qat/block200.dat", "qat/block201.dat", "qat/block202.dat", "qat/block203.dat",
"qat/block204.dat", "qat/block205.dat", "qat/block206.dat", "qat/block207.dat",
"qat/block208.dat", "qat/block209.dat", "qat/block20.dat", "qat/block210.dat",
"qat/block211.dat", "qat/block212.dat", "qat/block213.dat", "qat/block214.dat",
"qat/block215.dat", "qat/block216.dat", "qat/block217.dat", "qat/block218.dat",
"qat/block219.dat", "qat/block21.dat", "qat/block220.dat", "qat/block221.dat",
"qat/block222.dat", "qat/block223.dat", "qat/block224.dat", "qat/block225.dat",
"qat/block226.dat", "qat/block227.dat", "qat/block228.dat", "qat/block229.dat",
"qat/block22.dat", "qat/block230.dat", "qat/block231.dat", "qat/block232.dat",
"qat/block233.dat", "qat/block234.dat", "qat/block235.dat", "qat/block236.dat",
"qat/block237.dat", "qat/block238.dat", "qat/block239.dat", "qat/block23.dat",
"qat/block240.dat", "qat/block241.dat", "qat/block242.dat", "qat/block243.dat",
"qat/block244.dat", "qat/block245.dat", "qat/block246.dat", "qat/block247.dat",
"qat/block248.dat", "qat/block249.dat", "qat/block24.dat", "qat/block250.dat",
"qat/block251.dat", "qat/block252.dat", "qat/block253.dat", "qat/block254.dat",
"qat/block255.dat", "qat/block256.dat", "qat/block257.dat", "qat/block258.dat",
"qat/block259.dat", "qat/block25.dat", "qat/block260.dat", "qat/block261.dat",
"qat/block262.dat", "qat/block263.dat", "qat/block264.dat", "qat/block265.dat",
"qat/block266.dat", "qat/block267.dat", "qat/block268.dat", "qat/block269.dat",
"qat/block26.dat", "qat/block270.dat", "qat/block271.dat", "qat/block272.dat",
"qat/block273.dat", "qat/block274.dat", "qat/block275.dat", "qat/block276.dat",
"qat/block277.dat", "qat/block278.dat", "qat/block279.dat", "qat/block27.dat",
"qat/block280.dat", "qat/block281.dat", "qat/block282.dat", "qat/block283.dat",
"qat/block284.dat", "qat/block285.dat", "qat/block286.dat", "qat/block287.dat",
"qat/block288.dat", "qat/block289.dat", "qat/block28.dat", "qat/block290.dat",
"qat/block291.dat", "qat/block292.dat", "qat/block293.dat", "qat/block294.dat",
"qat/block295.dat", "qat/block296.dat", "qat/block297.dat", "qat/block298.dat",
"qat/block299.dat", "qat/block29.dat", "qat/block2.dat", "qat/block300.dat",
"qat/block301.dat", "qat/block302.dat", "qat/block303.dat", "qat/block304.dat",
"qat/block305.dat", "qat/block306.dat", "qat/block307.dat", "qat/block308.dat",
"qat/block309.dat", "qat/block30.dat", "qat/block310.dat", "qat/block311.dat",
"qat/block312.dat", "qat/block313.dat", "qat/block314.dat", "qat/block315.dat",
"qat/block316.dat", "qat/block317.dat", "qat/block318.dat", "qat/block319.dat",
"qat/block31.dat", "qat/block320.dat", "qat/block321.dat", "qat/block323.dat",
"qat/block324.dat", "qat/block325.dat", "qat/block326.dat", "qat/block327.dat",
"qat/block328.dat", "qat/block329.dat", "qat/block32.dat", "qat/block330.dat",
"qat/block331.dat", "qat/block332.dat", "qat/block333.dat", "qat/block334.dat",
"qat/block335.dat", "qat/block336.dat", "qat/block337.dat", "qat/block338.dat",
"qat/block339.dat", "qat/block33.dat", "qat/block340.dat", "qat/block341.dat",
"qat/block342.dat", "qat/block343.dat", "qat/block344.dat", "qat/block345.dat",
"qat/block346.dat", "qat/block347.dat", "qat/block348.dat", "qat/block349.dat",
"qat/block34.dat", "qat/block350.dat", "qat/block351.dat", "qat/block352.dat",
"qat/block353.dat", "qat/block354.dat", "qat/block355.dat", "qat/block356.dat",
"qat/block357.dat", "qat/block358.dat", "qat/block359.dat", "qat/block35.dat",
"qat/block360.dat", "qat/block361.dat", "qat/block362.dat", "qat/block363.dat",
"qat/block364.dat", "qat/block365.dat", "qat/block366.dat", "qat/block367.dat",
"qat/block368.dat", "qat/block369.dat", "qat/block36.dat", "qat/block370.dat",
"qat/block371.dat", "qat/block372.dat", "qat/block373.dat", "qat/block374.dat",
"qat/block375.dat", "qat/block376.dat", "qat/block377.dat", "qat/block378.dat",
"qat/block379.dat", "qat/block37.dat", "qat/block380.dat", "qat/block381.dat",
"qat/block382.dat", "qat/block383.dat", "qat/block384.dat", "qat/block385.dat",
"qat/block386.dat", "qat/block387.dat", "qat/block388.dat", "qat/block389.dat",
"qat/block38.dat", "qat/block390.dat", "qat/block391.dat", "qat/block392.dat",
"qat/block393.dat", "qat/block394.dat", "qat/block395.dat", "qat/block396.dat",
"qat/block397.dat", "qat/block398.dat", "qat/block399.dat", "qat/block39.dat",
"qat/block3.dat", "qat/block400.dat", "qat/block401.dat", "qat/block402.dat",
"qat/block403.dat", "qat/block404.dat", "qat/block405.dat", "qat/block406.dat",
"qat/block407.dat", "qat/block408.dat", "qat/block409.dat", "qat/block40.dat",
"qat/block410.dat", "qat/block411.dat", "qat/block412.dat", "qat/block413.dat",
"qat/block414.dat", "qat/block415.dat", "qat/block416.dat", "qat/block417.dat",
"qat/block418.dat", "qat/block419.dat", "qat/block41.dat", "qat/block420.dat",
"qat/block421.dat", "qat/block422.dat", "qat/block423.dat", "qat/block424.dat",
"qat/block425.dat", "qat/block426.dat", "qat/block427.dat", "qat/block428.dat",
"qat/block429.dat", "qat/block42.dat", "qat/block430.dat", "qat/block431.dat",
"qat/block432.dat", "qat/block433.dat", "qat/block434.dat", "qat/block435.dat",
"qat/block436.dat", "qat/block437.dat", "qat/block438.dat", "qat/block439.dat",
"qat/block43.dat", "qat/block440.dat", "qat/block441.dat", "qat/block442.dat",
"qat/block443.dat", "qat/block444.dat", "qat/block445.dat", "qat/block446.dat",
"qat/block447.dat", "qat/block448.dat", "qat/block449.dat", "qat/block44.dat",
"qat/block450.dat", "qat/block451.dat", "qat/block452.dat", "qat/block453.dat",
"qat/block454.dat", "qat/block455.dat", "qat/block456.dat", "qat/block457.dat",
"qat/block458.dat", "qat/block459.dat", "qat/block45.dat", "qat/block460.dat",
"qat/block461.dat", "qat/block462.dat", "qat/block463.dat", "qat/block464.dat",
"qat/block465.dat", "qat/block466.dat", "qat/block467.dat", "qat/block468.dat",
"qat/block469.dat", "qat/block46.dat", "qat/block470.dat", "qat/block471.dat",
"qat/block472.dat", "qat/block473.dat", "qat/block474.dat", "qat/block475.dat",
"qat/block476.dat", "qat/block477.dat", "qat/block478.dat", "qat/block479.dat",
"qat/block47.dat", "qat/block480.dat", "qat/block481.dat", "qat/block482.dat",
"qat/block483.dat", "qat/block484.dat", "qat/block485.dat", "qat/block486.dat",
"qat/block487.dat", "qat/block488.dat", "qat/block489.dat", "qat/block48.dat",
"qat/block490.dat", "qat/block491.dat", "qat/block493.dat", "qat/block494.dat",
"qat/block495.dat", "qat/block496.dat", "qat/block497.dat", "qat/block498.dat",
"qat/block499.dat", "qat/block49.dat", "qat/block4.dat", "qat/block500.dat",
"qat/block501.dat", "qat/block502.dat", "qat/block503.dat", "qat/block504.dat",
"qat/block505.dat", "qat/block506.dat", "qat/block507.dat", "qat/block508.dat",
"qat/block509.dat", "qat/block50.dat", "qat/block510.dat", "qat/block511.dat",
"qat/block512.dat", "qat/block513.dat", "qat/block514.dat", "qat/block515.dat",
"qat/block516.dat", "qat/block517.dat", "qat/block518.dat", "qat/block519.dat",
"qat/block51.dat", "qat/block520.dat", "qat/block521.dat", "qat/block522.dat",
"qat/block523.dat", "qat/block524.dat", "qat/block525.dat", "qat/block526.dat",
"qat/block527.dat", "qat/block528.dat", "qat/block529.dat", "qat/block52.dat",
"qat/block530.dat", "qat/block531.dat", "qat/block532.dat", "qat/block533.dat",
"qat/block534.dat", "qat/block535.dat", "qat/block536.dat", "qat/block537.dat",
"qat/block538.dat", "qat/block539.dat", "qat/block53.dat", "qat/block540.dat",
"qat/block541.dat", "qat/block542.dat", "qat/block543.dat", "qat/block544.dat",
"qat/block545.dat", "qat/block546.dat", "qat/block547.dat", "qat/block548.dat",
"qat/block549.dat", "qat/block54.dat", "qat/block550.dat", "qat/block551.dat",
"qat/block552.dat", "qat/block553.dat", "qat/block554.dat", "qat/block555.dat",
"qat/block556.dat", "qat/block557.dat", "qat/block558.dat", "qat/block559.dat",
"qat/block55.dat", "qat/block560.dat", "qat/block561.dat", "qat/block562.dat",
"qat/block563.dat", "qat/block564.dat", "qat/block565.dat", "qat/block566.dat",
"qat/block567.dat", "qat/block568.dat", "qat/block569.dat", "qat/block56.dat",
"qat/block570.dat", "qat/block571.dat", "qat/block572.dat", "qat/block573.dat",
"qat/block574.dat", "qat/block575.dat", "qat/block576.dat", "qat/block577.dat",
"qat/block578.dat", "qat/block579.dat", "qat/block57.dat", "qat/block580.dat",
"qat/block581.dat", "qat/block582.dat", "qat/block583.dat", "qat/block584.dat",
"qat/block585.dat", "qat/block586.dat", "qat/block587.dat", "qat/block588.dat",
"qat/block589.dat", "qat/block58.dat", "qat/block590.dat", "qat/block591.dat",
"qat/block592.dat", "qat/block593.dat", "qat/block594.dat", "qat/block595.dat",
"qat/block596.dat", "qat/block597.dat", "qat/block598.dat", "qat/block599.dat",
"qat/block59.dat", "qat/block5.dat", "qat/block600.dat", "qat/block601.dat",
"qat/block602.dat", "qat/block603.dat", "qat/block604.dat", "qat/block605.dat",
"qat/block606.dat", "qat/block607.dat", "qat/block608.dat", "qat/block609.dat",
"qat/block60.dat", "qat/block610.dat", "qat/block611.dat", "qat/block612.dat",
"qat/block613.dat", "qat/block614.dat", "qat/block617.dat", "qat/block618.dat",
"qat/block619.dat", "qat/block61.dat", "qat/block620.dat", "qat/block621.dat",
"qat/block622.dat", "qat/block623.dat", "qat/block624.dat", "qat/block625.dat",
"qat/block626.dat", "qat/block627.dat", "qat/block628.dat", "qat/block629.dat",
"qat/block62.dat", "qat/block630.dat", "qat/block631.dat", "qat/block632.dat",
"qat/block633.dat", "qat/block634.dat", "qat/block635.dat", "qat/block636.dat",
"qat/block637.dat", "qat/block638.dat", "qat/block639.dat", "qat/block63.dat",
"qat/block640.dat", "qat/block641.dat", "qat/block642.dat", "qat/block643.dat",
"qat/block644.dat", "qat/block645.dat", "qat/block646.dat", "qat/block647.dat",
"qat/block648.dat", "qat/block649.dat", "qat/block64.dat", "qat/block650.dat",
"qat/block651.dat", "qat/block652.dat", "qat/block653.dat", "qat/block654.dat",
"qat/block655.dat", "qat/block656.dat", "qat/block658.dat", "qat/block659.dat",
"qat/block65.dat", "qat/block660.dat", "qat/block661.dat", "qat/block662.dat",
"qat/block663.dat", "qat/block664.dat", "qat/block665.dat", "qat/block666.dat",
"qat/block667.dat", "qat/block668.dat", "qat/block669.dat", "qat/block66.dat",
"qat/block670.dat", "qat/block671.dat", "qat/block672.dat", "qat/block673.dat",
"qat/block674.dat", "qat/block675.dat", "qat/block676.dat", "qat/block677.dat",
"qat/block678.dat", "qat/block679.dat", "qat/block67.dat", "qat/block680.dat",
"qat/block681.dat", "qat/block682.dat", "qat/block683.dat", "qat/block684.dat",
"qat/block685.dat", "qat/block686.dat", "qat/block687.dat", "qat/block688.dat",
"qat/block689.dat", "qat/block68.dat", "qat/block690.dat", "qat/block691.dat",
"qat/block692.dat", "qat/block693.dat", "qat/block694.dat", "qat/block695.dat",
"qat/block696.dat", "qat/block697.dat", "qat/block698.dat", "qat/block699.dat",
"qat/block69.dat", "qat/block6.dat", "qat/block700.dat", "qat/block701.dat",
"qat/block702.dat", "qat/block703.dat", "qat/block704.dat", "qat/block705.dat",
"qat/block706.dat", "qat/block707.dat", "qat/block708.dat", "qat/block709.dat",
"qat/block70.dat", "qat/block710.dat", "qat/block711.dat", "qat/block712.dat",
"qat/block713.dat", "qat/block714.dat", "qat/block715.dat", "qat/block716.dat",
"qat/block717.dat", "qat/block718.dat", "qat/block719.dat", "qat/block71.dat",
"qat/block720.dat", "qat/block721.dat", "qat/block722.dat", "qat/block723.dat",
"qat/block724.dat", "qat/block725.dat", "qat/block726.dat", "qat/block727.dat",
"qat/block728.dat", "qat/block729.dat", "qat/block72.dat", "qat/block730.dat",
"qat/block731.dat", "qat/block732.dat", "qat/block733.dat", "qat/block734.dat",
"qat/block735.dat", "qat/block736.dat", "qat/block737.dat", "qat/block738.dat",
"qat/block739.dat", "qat/block73.dat", "qat/block740.dat", "qat/block741.dat",
"qat/block742.dat", "qat/block743.dat", "qat/block744.dat", "qat/block745.dat",
"qat/block746.dat", "qat/block747.dat", "qat/block748.dat", "qat/block749.dat",
"qat/block74.dat", "qat/block750.dat", "qat/block752.dat", "qat/block753.dat",
"qat/block754.dat", "qat/block755.dat", "qat/block756.dat", "qat/block757.dat",
"qat/block758.dat", "qat/block759.dat", "qat/block75.dat", "qat/block760.dat",
"qat/block761.dat", "qat/block762.dat", "qat/block763.dat", "qat/block764.dat",
"qat/block766.dat", "qat/block767.dat", "qat/block768.dat", "qat/block769.dat",
"qat/block76.dat", "qat/block770.dat", "qat/block771.dat", "qat/block772.dat",
"qat/block773.dat", "qat/block774.dat", "qat/block775.dat", "qat/block776.dat",
"qat/block777.dat", "qat/block778.dat", "qat/block779.dat", "qat/block77.dat",
"qat/block780.dat", "qat/block781.dat", "qat/block782.dat", "qat/block783.dat",
"qat/block784.dat", "qat/block785.dat", "qat/block786.dat", "qat/block787.dat",
"qat/block788.dat", "qat/block789.dat", "qat/block78.dat", "qat/block790.dat",
"qat/block791.dat", "qat/block792.dat", "qat/block793.dat", "qat/block794.dat",
"qat/block795.dat", "qat/block796.dat", "qat/block797.dat", "qat/block798.dat",
"qat/block799.dat", "qat/block79.dat", "qat/block7.dat", "qat/block800.dat",
"qat/block801.dat", "qat/block802.dat", "qat/block803.dat", "qat/block804.dat",
"qat/block805.dat", "qat/block806.dat", "qat/block807.dat", "qat/block808.dat",
"qat/block809.dat", "qat/block80.dat", "qat/block810.dat", "qat/block811.dat",
"qat/block812.dat", "qat/block813.dat", "qat/block814.dat", "qat/block815.dat",
"qat/block816.dat", "qat/block817.dat", "qat/block818.dat", "qat/block819.dat",
"qat/block81.dat", "qat/block820.dat", "qat/block821.dat", "qat/block822.dat",
"qat/block823.dat", "qat/block824.dat", "qat/block825.dat", "qat/block826.dat",
"qat/block827.dat", "qat/block828.dat", "qat/block829.dat", "qat/block82.dat",
"qat/block830.dat", "qat/block831.dat", "qat/block832.dat", "qat/block833.dat",
"qat/block834.dat", "qat/block835.dat", "qat/block836.dat", "qat/block837.dat",
"qat/block838.dat", "qat/block839.dat", "qat/block83.dat", "qat/block840.dat",
"qat/block841.dat", "qat/block842.dat", "qat/block843.dat", "qat/block844.dat",
"qat/block845.dat", "qat/block846.dat", "qat/block847.dat", "qat/block848.dat",
"qat/block849.dat", "qat/block84.dat", "qat/block850.dat", "qat/block851.dat",
"qat/block852.dat", "qat/block853.dat", "qat/block854.dat", "qat/block855.dat",
"qat/block856.dat", "qat/block857.dat", "qat/block858.dat", "qat/block859.dat",
"qat/block85.dat", "qat/block860.dat", "qat/block861.dat", "qat/block862.dat",
"qat/block863.dat", "qat/block864.dat", "qat/block865.dat", "qat/block866.dat",
"qat/block867.dat", "qat/block868.dat", "qat/block869.dat", "qat/block86.dat",
"qat/block870.dat", "qat/block871.dat", "qat/block872.dat", "qat/block873.dat",
"qat/block874.dat", "qat/block875.dat", "qat/block876.dat", "qat/block877.dat",
"qat/block878.dat", "qat/block879.dat", "qat/block87.dat", "qat/block880.dat",
"qat/block881.dat", "qat/block882.dat", "qat/block883.dat", "qat/block884.dat",
"qat/block885.dat", "qat/block886.dat", "qat/block887.dat", "qat/block888.dat",
"qat/block889.dat", "qat/block890.dat", "qat/block891.dat", "qat/block892.dat",
"qat/block893.dat", "qat/block894.dat", "qat/block895.dat", "qat/block897.dat",
"qat/block898.dat", "qat/block899.dat", "qat/block89.dat", "qat/block8.dat",
"qat/block900.dat", "qat/block901.dat", "qat/block902.dat", "qat/block903.dat",
"qat/block904.dat", "qat/block905.dat", "qat/block906.dat", "qat/block907.dat",
"qat/block908.dat", "qat/block909.dat", "qat/block90.dat", "qat/block910.dat",
"qat/block911.dat", "qat/block912.dat", "qat/block913.dat", "qat/block914.dat",
"qat/block915.dat", "qat/block916.dat", "qat/block917.dat", "qat/block918.dat",
"qat/block919.dat", "qat/block91.dat", "qat/block920.dat", "qat/block921.dat",
"qat/block922.dat", "qat/block923.dat", "qat/block924.dat", "qat/block925.dat",
"qat/block926.dat", "qat/block927.dat", "qat/block928.dat", "qat/block929.dat",
"qat/block92.dat", "qat/block930.dat", "qat/block931.dat", "qat/block932.dat",
"qat/block933.dat", "qat/block934.dat", "qat/block935.dat", "qat/block936.dat",
"qat/block937.dat", "qat/block938.dat", "qat/block939.dat", "qat/block93.dat",
"qat/block940.dat", "qat/block941.dat", "qat/block942.dat", "qat/block943.dat",
"qat/block944.dat", "qat/block945.dat", "qat/block946.dat", "qat/block947.dat",
"qat/block948.dat", "qat/block949.dat", "qat/block94.dat", "qat/block950.dat",
"qat/block951.dat", "qat/block952.dat", "qat/block954.dat", "qat/block955.dat",
"qat/block956.dat", "qat/block957.dat", "qat/block958.dat", "qat/block959.dat",
"qat/block95.dat", "qat/block960.dat", "qat/block961.dat", "qat/block962.dat",
"qat/block963.dat", "qat/block964.dat", "qat/block965.dat", "qat/block966.dat",
"qat/block967.dat", "qat/block968.dat", "qat/block969.dat", "qat/block96.dat",
"qat/block970.dat", "qat/block971.dat", "qat/block972.dat", "qat/block973.dat",
"qat/block974.dat", "qat/block975.dat", "qat/block976.dat", "qat/block977.dat",
"qat/block978.dat", "qat/block979.dat", "qat/block97.dat", "qat/block980.dat",
"qat/block981.dat", "qat/block982.dat", "qat/block983.dat", "qat/block984.dat",
"qat/block985.dat", "qat/block986.dat", "qat/block987.dat", "qat/block988.dat",
"qat/block989.dat", "qat/block98.dat", "qat/block991.dat", "qat/block992.dat",
"qat/block993.dat", "qat/block994.dat", "qat/block995.dat", "qat/block996.dat",
"qat/block997.dat", "qat/block998.dat", "qat/block999.dat", "qat/block99.dat",
"qat/block9.dat"
    };

char *badFiles[] = {"badFiles/2bit_dist_code_wrap_bug",
        "badFiles/local",
        "badFiles/file10",
        "badFiles/local_local_gz_four_16bit_code_lengths",
        "badFiles/file11",
        "badFiles/local_lt64k",
        "badFiles/file12",
        "badFiles/onebit_code_ex2",
        "badFiles/file13",
        "badFiles/one_max_cnt_local",
        "badFiles/four_16bit_code_lengths"};

Cpa32U sizeOfBadFiles = sizeof(badFiles)/sizeof(char *);

#define FUZZ_FILE_PATH_SIZE 50
static char fuzzFileName[FUZZ_FILE_PATH_SIZE ] = "fuzz/local";
#ifdef USER_SPACE

CpaStatus printFuzzFile(void)
{
    PRINT("FuzzFile: %s\n",fuzzFileName);
    return CPA_STATUS_SUCCESS;
}

CpaStatus setFuzzFile(char* fileName)
{
    strncpy(fuzzFileName, fileName, FUZZ_FILE_PATH_SIZE );
    return CPA_STATUS_SUCCESS;
}
#endif
#endif

CpaStatus startDcServices(Cpa32U buffSize)

{
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa32U size= 0;
    Cpa32U i = 0, k = 0;
    Cpa32U nodeId = 0;
    Cpa32U nProcessorsOnline = 0;
    Cpa16U numBuffers = 0;
    CpaBufferList **tempBufferList = NULL;
#ifdef DEBUG_CODE
    PRINT("***********************************\n");
    PRINT("Entered Function:: %s\n",__FUNCTION__);
    PRINT(" Buffers Size in %s = %d\n", __FUNCTION__, buffSize);
#endif

    /*if the service started flag is false*/
    if(dc_service_started_g == CPA_FALSE)
    {
        /* Get the number of DC Instances */
        status = cpaDcGetNumInstances(&numDcInstances_g);
        /* Check the status */
        if(CPA_STATUS_SUCCESS != status)
        {
            PRINT_ERR("Unable to Get Number of DC instances\n");
            return CPA_STATUS_FAIL;
        }
        /* Check if at least one DC instance are present */
        if( 0 == numDcInstances_g )
        {
            PRINT_ERR(" DC Instances are not present\n");
            return CPA_STATUS_FAIL;
        }
        /* Allocate memory for all the instances */
        dcInstances_g = qaeMemAlloc(sizeof(CpaInstanceHandle)*numDcInstances_g);
        /* Check For NULL */
        if( NULL == dcInstances_g)
        {
            PRINT_ERR(" Unable to allocate memory for Instances \n");
            return CPA_STATUS_FAIL;
        }
#ifdef DEBUG_CODE
        allocate_g++;
#endif
        /* Get DC Instances */
        status = cpaDcGetInstances(numDcInstances_g, dcInstances_g);
        /* Check Status */
        if(CPA_STATUS_SUCCESS != status)
        {
            PRINT_ERR("Unable to Get DC instances\n");
            qaeMemFree((void**)&dcInstances_g);
            return CPA_STATUS_FAIL;
        }

        /* Allocate the buffer list pointers to the number of Instances
         * this buffer list list is used only in case of dynamic
         * compression
         */
        pInterBuffList_g = (CpaBufferList ***)qaeMemAlloc(numDcInstances_g
                * sizeof(CpaBufferList **));
        /* Check For NULL */
        if(NULL == pInterBuffList_g)
        {
            PRINT_ERR("Unable to allocate dynamic buffer List\n");
            qaeMemFree((void**)&dcInstances_g);
            return CPA_STATUS_FAIL;
        }
#ifdef DEBUG_CODE
        allocate_g++;
#endif
        /* Start the Loop to create Buffer List for each instance*/
        for(i = 0; i < numDcInstances_g; i++)
        {
            /* get the Node ID for each instance Handle */
            status = sampleCodeDcGetNode(dcInstances_g[i], &nodeId);
            if(CPA_STATUS_SUCCESS != status)
            {
                PRINT_ERR("Unable to get NodeId\n");
                qaeMemFree((void**)&dcInstances_g);
                qaeMemFree((void**)&pInterBuffList_g);
                return CPA_STATUS_FAIL;
            }
            status = cpaDcGetNumIntermediateBuffers(dcInstances_g[i],&numBuffers);
            if( CPA_STATUS_SUCCESS != status )
            {
                PRINT_ERR("Unable to allocate Memory for Dynamic Buffer\n");
                qaeMemFree((void**)&dcInstances_g);
                qaeMemFree((void**)&pInterBuffList_g);
                return CPA_STATUS_FAIL;
            }

            /* allocate the buffer list memory for the dynamic Buffers */
             pInterBuffList_g[i] =
                qaeMemAllocNUMA(sizeof(CpaBufferList *) * numBuffers,
                        nodeId, BYTE_ALIGNMENT_64);
            if( NULL == pInterBuffList_g[i])
            {
                PRINT_ERR("Unable to allocate Memory for Dynamic Buffer\n");
                qaeMemFree((void**)&dcInstances_g);
                qaeMemFree((void**)&pInterBuffList_g);
                return CPA_STATUS_FAIL;
            }
#ifdef DEBUG_CODE
            allocate_g++;
#endif
            /* get the size of the Private meta data
             * needed to create Buffer List
             */
            status = cpaDcBufferListGetMetaSize(dcInstances_g[i],
                    numBuffers, &size);
            if(CPA_STATUS_SUCCESS != status )
            {
                PRINT_ERR("Get Meta Size Data Failed\n");
                qaeMemFree((void**)&dcInstances_g);
                qaeMemFree((void**)&pInterBuffList_g);
                return CPA_STATUS_FAIL;
            }
#ifdef DEBUG_CODE
            PRINT("After cpaDCBufferListGetMeta\n");
            PRINT("MetaSize=%d\n", size);
#endif
            tempBufferList = pInterBuffList_g[i];
            for(k = 0;k < numBuffers;k++)
            {
                tempBufferList[k] = (CpaBufferList *)
                qaeMemAllocNUMA(sizeof(CpaBufferList),
                        nodeId, BYTE_ALIGNMENT_64);
                if(NULL == tempBufferList[k])
                {
                    PRINT(" %s:: Unable to allocate memory for "
                            "tempBufferList\n", __FUNCTION__);
                    qaeMemFree((void**)&dcInstances_g);
                    freeDcBufferList(tempBufferList , k+1);
                    qaeMemFree((void**)&pInterBuffList_g);
                    return CPA_STATUS_FAIL;
                }
#ifdef DEBUG_CODE
                allocate_g++;
#endif
                tempBufferList[k]->pPrivateMetaData =
                qaeMemAllocNUMA(size, nodeId, BYTE_ALIGNMENT_64);
                if(NULL == tempBufferList[k]->pPrivateMetaData)
                {
                    PRINT(" %s:: Unable to allocate memory for "
                            "pPrivateMetaData\n", __FUNCTION__);
                    qaeMemFree((void**)&dcInstances_g);
                    freeDcBufferList(tempBufferList , k+1);
                    qaeMemFree((void**)&pInterBuffList_g);
                    return CPA_STATUS_FAIL;
                }
#ifdef DEBUG_CODE
                allocate_g++;
#endif
                tempBufferList[k]->numBuffers = ONE_BUFFER_DC;
                /* allocate flat buffers */
                tempBufferList[k]->pBuffers =
                qaeMemAllocNUMA((sizeof(CpaFlatBuffer)),
                        nodeId, BYTE_ALIGNMENT_64);
                if(NULL == tempBufferList[k]->pBuffers )
                {
                    PRINT_ERR("Unable to allocate memory for pBuffers\n");
                    qaeMemFree((void**)&dcInstances_g);
                    freeDcBufferList(tempBufferList , k+1);
                    qaeMemFree((void**)&pInterBuffList_g);
                    return CPA_STATUS_FAIL;
                }
#ifdef DEBUG_CODE
                allocate_g++;
#endif

                tempBufferList[k]->pBuffers[0].pData =
                qaeMemAllocNUMA(expansionFactor_g*EXTRA_BUFFER*buffSize,nodeId, BYTE_ALIGNMENT_64);
                if( NULL == pInterBuffList_g[i])
                {
                    PRINT_ERR("Unable to allocate Memory for pBuffers\n");
                    qaeMemFree((void**)&dcInstances_g);
                    freeDcBufferList(tempBufferList , k+1);
                    qaeMemFree((void**)&pInterBuffList_g);
                    return CPA_STATUS_FAIL;
                }
#ifdef DEBUG_CODE
                allocate_g++;
#endif
                tempBufferList[k]->pBuffers[0].dataLenInBytes =
                    expansionFactor_g*EXTRA_BUFFER *buffSize;
            }

           /* When starting the DC Instance, the API expects that the
           * private meta data should be greater than the dataLength
           */
            /* Configure memory Configuration Function */
            status = cpaDcSetAddressTranslation(dcInstances_g[i],
                    (CpaVirtualToPhysical)qaeVirtToPhysNUMA);
            if(CPA_STATUS_SUCCESS != status )
            {
                PRINT_ERR("Error setting memory config for instance\n");
                qaeMemFree((void**)&dcInstances_g);
                freeDcBufferList(pInterBuffList_g[i], numBuffers);
                qaeMemFreeNUMA((void**)&pInterBuffList_g[i]);
                qaeMemFree((void**)&pInterBuffList_g);
                return CPA_STATUS_FAIL;
            }
            /* Start DC Instance */
            status = cpaDcStartInstance(dcInstances_g[i],
                    numBuffers, pInterBuffList_g[i]);
            if(CPA_STATUS_SUCCESS != status)
            {
                PRINT_ERR("Unable to start DC Instance\n");
                qaeMemFree((void**)&dcInstances_g);
                freeDcBufferList(pInterBuffList_g[i], numBuffers);
                qaeMemFreeNUMA((void**)&pInterBuffList_g[i]);
                qaeMemFree((void**)&pInterBuffList_g);
                return CPA_STATUS_FAIL;
            }
        }
        /*set the started flag to true*/
        dc_service_started_g = CPA_TRUE;
    }

#ifdef DEBUG_CODE
    PRINT("Exit From Function:: %s\n",__FUNCTION__);
    PRINT("***********************************\n");
#endif
    /*determine number of cores on system and limit the number of cores to be
     * used to be the smaller of the numberOf Instances or the number of cores*/
    nProcessorsOnline  = (Cpa32U)sysconf(_SC_NPROCESSORS_ONLN);
    if(nProcessorsOnline > numDcInstances_g)
    {
        g_coreLimit = numDcInstances_g;
    }
    return status;
}

/*stop all acceleration services*/
CpaStatus stopDcServices()
{
    Cpa32U i = 0, j = 0;
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaBufferList **tempBufferList = NULL;
    Cpa16U numBuffers = 0;
#ifdef DEBUG_CODE
    PRINT("***********************************\n");
    PRINT("Entered Function:: %s\n",__FUNCTION__);
#endif

    /* set polling flag to default */
    dc_polling_started_g = CPA_FALSE;
    /*stop only if the services is in a started state*/
    if(dc_service_started_g ==CPA_TRUE)
    {
        for(i = 0; i < numDcInstances_g; i++)
        {
            /* Free the Dynamic Buffers allocated
             * while starting DC Services
             */
            tempBufferList = pInterBuffList_g[i];
            status = cpaDcGetNumIntermediateBuffers(dcInstances_g[i],&numBuffers);
            for(j = 0; j < numBuffers; j++)
            {

                qaeMemFreeNUMA((void**)&tempBufferList[j]->pBuffers->pData);
#ifdef DEBUG_CODE
                free_g++;
#endif
                qaeMemFreeNUMA((void**)&tempBufferList[j]->pPrivateMetaData);
#ifdef DEBUG_CODE
                free_g++;
#endif
                qaeMemFreeNUMA((void**)&tempBufferList[j]->pBuffers);
#ifdef DEBUG_CODE
                free_g++;
#endif
                qaeMemFreeNUMA((void**)&tempBufferList[j]);
#ifdef DEBUG_CODE
                free_g++;
#endif
            }
           /* free the buffer List*/
           qaeMemFreeNUMA((void**)&pInterBuffList_g[i]);
#ifdef DEBUG_CODE
           free_g++;
#endif
                /*stop all instances*/
           cpaDcStopInstance(dcInstances_g[i]);
        }
        qaeMemFree((void**)&pInterBuffList_g);
#ifdef DEBUG_CODE
        free_g++;
#endif
        /*set the service started flag to false*/
        dc_service_started_g = CPA_FALSE;
    }
    /* Wait for all threads_g to complete */
    for (i=0; i < numDcPolledInstances_g; i++)
    {
        pthread_join(dcPollingThread_g[i], NULL);

    }
    if(numDcPolledInstances_g > 0)
    {
        qaeMemFree((void**)&dcPollingThread_g);
#ifdef DEBUG_CODE
        free_g++;
#endif
        numDcPolledInstances_g = 0;
    }
    if ( dcInstances_g != NULL )
    {
        qaeMemFree((void**)&dcInstances_g);
        dcInstances_g = NULL;
#ifdef DEBUG_CODE
        free_g++;
#endif
    }
#ifdef DEBUG_CODE
    PRINT("Exit From Function:: %s\n",__FUNCTION__);
    PRINT("***********************************\n");
#endif
    return status;
}

CpaStatus dcCreatePollingThreadsIfPollingIsEnabled(void)
{
    CpaInstanceInfo2 *instanceInfo2 = NULL;
    Cpa16U i = 0, numCreatedPollingThreads = 0;
    //Cpa32U coreAffinity = 0;
    CpaStatus status = CPA_STATUS_SUCCESS;
    performance_func_t *pollFnArr = NULL;
#if defined(USER_SPACE) && !defined(SC_EPOLL_DISABLED)
    int fd = -1;
#endif

    if (CPA_FALSE == dc_polling_started_g)
    {
        instanceInfo2 = qaeMemAlloc(numDcInstances_g *
                sizeof(CpaInstanceInfo2));
        if(NULL == instanceInfo2)
        {
            PRINT_ERR("Failed to allocate memory for pInstanceInfo2");
            return CPA_STATUS_FAIL;
        }
        pollFnArr = qaeMemAlloc(numDcInstances_g * sizeof(performance_func_t));
        if(NULL == pollFnArr)
        {
            PRINT_ERR("Failed to allocate memory for polling functions\n");
            qaeMemFree((void**)&instanceInfo2);
            return CPA_STATUS_FAIL;
        }
        for(i = 0; i < numDcInstances_g; i++)
        {
            status = cpaDcInstanceGetInfo2(dcInstances_g[i], &instanceInfo2[i]);
            if(CPA_STATUS_SUCCESS != status)
            {
                qaeMemFree((void**)&instanceInfo2);
                qaeMemFree((void**)&pollFnArr);
                return CPA_STATUS_FAIL;
            }
            pollFnArr[i] = NULL;
            if(CPA_TRUE == instanceInfo2[i].isPolled)
            {
                numDcPolledInstances_g++;
#if defined(USER_SPACE) && !defined(SC_EPOLL_DISABLED)
                status = icp_sal_DcGetFileDescriptor(dcInstances_g[i], &fd);
                if (CPA_STATUS_SUCCESS == status)
                {
                    pollFnArr[i] = sampleCodeDcEventPoll;
                    icp_sal_DcPutFileDescriptor(dcInstances_g[i], fd);
                    continue;

                }
                else if(CPA_STATUS_FAIL == status)
                {
                    PRINT_ERR("Error getting file descriptor for Event based "
                            "instance #%d\n", i);
                    qaeMemFree((void**)&instanceInfo2);
                    qaeMemFree((void**)&pollFnArr);
                    return CPA_STATUS_FAIL;

                }
                /* else feature is unsupported and sampleCodeDcPoll() is to be
                 * used.
                 */
#endif
                pollFnArr[i] = sampleCodeDcPoll;
            }

        }
        if (0 == numDcPolledInstances_g)
        {
            qaeMemFree((void**)&instanceInfo2);
            qaeMemFree((void**)&pollFnArr);
            return CPA_STATUS_SUCCESS;
        }
        dcPollingThread_g =
            qaeMemAlloc(numDcPolledInstances_g * sizeof(sample_code_thread_t));
        if(NULL == dcPollingThread_g)
        {
            PRINT_ERR("Failed to allocate memory for polling threads\n");
            qaeMemFree((void**)&instanceInfo2);
            qaeMemFree((void**)&pollFnArr);
            return CPA_STATUS_FAIL;
        }
        for(i = 0; i < numDcInstances_g; i++)
        {
            if(NULL != pollFnArr[i])
            {
                status = dc_threadCreate(
                        &dcPollingThread_g[numCreatedPollingThreads],
                        NULL,
                        pollFnArr[i],
                        dcInstances_g[i]);
                if(status != CPA_STATUS_SUCCESS)
                {
                    PRINT_ERR("Error starting polling thread %d\n", status);
                    /*attempt to stop any started service, we dont check status
                     * as some instances may not have been started and
                     * this might return fail*/
                    qaeMemFree((void**)&instanceInfo2);
                    qaeMemFree((void**)&pollFnArr);
                    return CPA_STATUS_FAIL;
                }
                numCreatedPollingThreads++;
            }
        }
        qaeMemFree((void**)&instanceInfo2);
        qaeMemFree((void**)&pollFnArr);
        dc_polling_started_g = CPA_TRUE;
    }
    return CPA_STATUS_SUCCESS;
}

CpaStatus dcDpPollNumOperations(perf_data_t *pPerfData,
                CpaInstanceHandle instanceHandle, Cpa64U numOperations)
{
    CpaStatus status = CPA_STATUS_FAIL;

    perf_cycles_t startCycles = 0, totalCycles = 0;
    Cpa32U freq = 2000;
    startCycles = dc_getTimestamp();

    while( pPerfData->responses != numOperations )
    {
        status = icp_sal_DcPollDpInstance(instanceHandle,0);
        if(CPA_STATUS_FAIL == status)
        {
            PRINT_ERR("Error polling instance\n");
            return CPA_STATUS_FAIL;
        }
        if(CPA_STATUS_RETRY == status)
        {
            AVOID_SOFTLOCKUP;
        }
        totalCycles = (dc_getTimestamp() - startCycles);
        if(totalCycles > 0)
        {
            do_div(totalCycles,freq);
        }

        if(totalCycles > SAMPLE_CODE_WAIT_THIRTY_SEC)
        {
            PRINT_ERR("Timeout on polling remaining Operations\n");
            PRINT("Expected %llu responses, revieved %llu\n",
                    (unsigned long long )numOperations,
                    (unsigned long long )pPerfData->responses);
            return CPA_STATUS_FAIL;
        }
    }
    return CPA_STATUS_SUCCESS;
}

CpaStatus waitForSemaphore(perf_data_t *perfData)
{
    Cpa64S responsesReceived = INITIAL_RESPONSE_COUNT;
    CpaStatus status = CPA_STATUS_SUCCESS;

#ifdef DEBUG_CODE
    PRINT("***********************************\n");
    PRINT("Entered Function:: %s\n",__FUNCTION__);
#endif
    /*wait for the callback to receive all responses and free the
     * semaphore, or if in sync mode, the semaphore should already be free*/

    while(sampleCodeSemaphoreWait(&perfData->comp,
            SAMPLE_CODE_WAIT_THIRTY_SEC)
            != CPA_STATUS_SUCCESS)
    {
#ifdef DEBUG_CODE
        PRINT("waiting on semaphore\n");
#endif
        if(INITIAL_RESPONSE_COUNT != responsesReceived &&
                responsesReceived != (Cpa64S)perfData->numOperations &&
                responsesReceived == (Cpa64S)perfData->responses)
        {
            PRINT_ERR("System is not responding\n");
            PRINT("Responses expected/received: %llu/%llu\n",
                    (unsigned long long)perfData->numOperations,
                    (unsigned long long)perfData->responses);
            status = CPA_STATUS_FAIL;
            break;
        }
        else
        {
            responsesReceived = perfData->responses;
        }
    }

#ifdef DEBUG_CODE
    PRINT("Exit from Function:: %s\n",__FUNCTION__);
    PRINT("***********************************\n");
#endif
    return status;
}

CpaStatus  sampleCodeDcGetNode(CpaInstanceHandle instanceHandle, Cpa32U *node)
{
    CpaStatus status = CPA_STATUS_FAIL;
    CpaInstanceInfo2 pInstanceInfo2;
#ifdef DEBUG_CODE
    PRINT("***********************************\n");
    PRINT("Entered Function:: %s\n",__FUNCTION__);
#endif

    status = cpaDcInstanceGetInfo2(instanceHandle, &pInstanceInfo2);
    if(CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("Unable to get Node affinity\n");
        return status;
    }
    *node = pInstanceInfo2.nodeAffinity;
#ifdef DEBUG_CODE
    PRINT("Exit From Function:: %s\n",__FUNCTION__);
    PRINT("***********************************\n");
#endif

    return status;
}

/* Change to a compression callback tag with parameter for poll interval */
void sampleCodeDcPoll(CpaInstanceHandle instanceHandle_in)
{
    CpaStatus status = CPA_STATUS_FAIL;
    struct timespec reqTime,remTime;
    reqTime.tv_sec = 0;
    reqTime.tv_nsec = DEFAULT_POLL_INTERVAL_NSEC;
    while(dc_service_started_g == CPA_TRUE)
    {
        /*poll for 0 means process all packets on the ET ring */
        status = icp_sal_DcPollInstance(instanceHandle_in, 0);
        if(CPA_STATUS_SUCCESS == status || CPA_STATUS_RETRY == status)
        {
            /* do nothing */
        }
        else
        {
            PRINT_ERR("WARNING icp_sal_DcPollInstance returned status %d\n",
                    status);
        }
        nanosleep(&reqTime,&remTime);

    }
    pthread_exit(NULL);
}

/*This function tells the compression sample code to use zLib software to
 * compress the data prior to calling the decompression*/
CpaStatus useZlib(void)
{
#ifdef USE_ZLIB
    useZlib_g = CPA_TRUE;
#else
#endif
    return CPA_STATUS_SUCCESS;
}
EXPORT_SYMBOL(useZlib);

/*This function tells the compression sample code to use zLib software to
 * compress the data prior to calling the decompression*/
CpaStatus useAccelCompression(void)
{
    useZlib_g = CPA_FALSE;
    return CPA_STATUS_SUCCESS;
}
EXPORT_SYMBOL(useAccelCompression);

#ifdef ZERO_BYTE_LAST_REQUEST
CpaStatus enableZeroByteRequest(void)
{
    zeroByteLastRequest_g = CPA_TRUE;
    return CPA_STATUS_SUCCESS;
}
EXPORT_SYMBOL(enableZeroByteRequest);

CpaStatus disableZeroByteRequest(void)
{
    zeroByteLastRequest_g = CPA_FALSE;
    return CPA_STATUS_SUCCESS;
}
EXPORT_SYMBOL(disableZeroByteRequest);
#endif
/*****************************************************************************
* * @description
* Poll the number of dc operations
* ***************************************************************************/
CpaStatus dcPollNumOperations(perf_data_t *pPerfData,
                CpaInstanceHandle instanceHandle, Cpa64U numOperations)
{
    CpaStatus status = CPA_STATUS_FAIL;

    perf_cycles_t startCycles = 0, totalCycles = 0;
    Cpa32U freq = 2000;
    startCycles = dc_getTimestamp();

    while( pPerfData->responses != numOperations )
    {
        status = icp_sal_DcPollInstance(instanceHandle,0);
        if(CPA_STATUS_FAIL == status)
        {
            PRINT_ERR("Error polling instance\n");
            return CPA_STATUS_FAIL;
        }
        if(CPA_STATUS_RETRY == status)
        {
            AVOID_SOFTLOCKUP;
        }
        totalCycles = (dc_getTimestamp() - startCycles);
        if(totalCycles > 0)
        {
            do_div(totalCycles,freq);
        }

        if(totalCycles > SAMPLE_CODE_WAIT_THIRTY_SEC)
        {
            PRINT_ERR("Timeout on polling remaining Operations\n");
            return CPA_STATUS_FAIL;
        }
    }
    return CPA_STATUS_SUCCESS;
}

CpaStatus dynamicHuffmanEnabled(CpaInstanceHandle *dcInstanceHandle,
        CpaBoolean *isEnabled)
{
    CpaDcInstanceCapabilities capabilities;
    CpaStatus status = CPA_STATUS_FAIL;
    CpaInstanceHandle pLocalDcInstanceHandle = NULL;
    Cpa16U numInstances = 0;

    memset(&capabilities, 0, sizeof(CpaDcInstanceCapabilities));

    /* Initialise to CPA_FALSE */
    *isEnabled = CPA_FALSE;

    if(NULL == dcInstanceHandle)
    {
        status = cpaDcGetNumInstances(&numInstances);
        if(CPA_STATUS_SUCCESS != status)
        {
            PRINT_ERR("Unable to check if dynamic Huffman is enabled, "
                    "cpaDcGetNumInstances failed with status: %d\n",
                    status);
            return CPA_STATUS_FAIL;
        }
        if(0 == numInstances)
        {
            PRINT_ERR("Unable to check if dynamic Huffman is enabled, "
                    "No DC instances available");
            return CPA_STATUS_FAIL;
        }
        status = cpaDcGetInstances(1, &pLocalDcInstanceHandle);
        if(CPA_STATUS_SUCCESS != status)
        {
            PRINT_ERR("Unable to check if dynamic Huffman is enabled, "
                    "cpaDcGetInstances failed with status: %d""\n",
                    status);
            return CPA_STATUS_FAIL;
        }
    }
    else
    {
        pLocalDcInstanceHandle = *dcInstanceHandle;
    }
    status = cpaDcQueryCapabilities(pLocalDcInstanceHandle, &capabilities);
    if(CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("Unable to check if dynamic Huffman is enabled, "
                "cpaDcQueryCapabilities failed with status: %d""\n",
                status);
        return CPA_STATUS_FAIL;
    }
    if(CPA_TRUE == capabilities.dynamicHuffman)
    {
        *isEnabled = CPA_TRUE;
    }
    return CPA_STATUS_SUCCESS;
}
