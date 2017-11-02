// ThreadUtility.cpp

#include "ThreadUtility.h"

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <assert.h>
#include <CoreServices/CoreServices.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>

void setThreadPriority(pthread_t inThread, uint32_t inPriority, bool inIsFixed)
{
	// on null thread argument, set the priority of the current thread
	pthread_t threadToAffect = inThread ? inThread : pthread_self();
	
    if (inPriority == 96)
    {
        // REAL-TIME / TIME-CONSTRAINT THREAD
        thread_time_constraint_policy_data_t    theTCPolicy;
		
		// times here are specified in Mach absolute time units.
		mach_timebase_info_data_t sTimebaseInfo;
		mach_timebase_info(&sTimebaseInfo);

		uint64_t period_nano = 1000*1000;
		uint64_t period_abs = (double)period_nano * (double)sTimebaseInfo.denom / (double)sTimebaseInfo.numer;

		theTCPolicy.period = period_abs;
        theTCPolicy.computation = period_abs/64;
        theTCPolicy.constraint = period_abs/4;
        theTCPolicy.preemptible = true;
        thread_policy_set (pthread_mach_thread_np(threadToAffect), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&theTCPolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    }
    else
    {
        // OTHER THREADS
        thread_extended_policy_data_t   theFixedPolicy;
        thread_precedence_policy_data_t   thePrecedencePolicy;
        int32_t                relativePriority;

        // [1] SET FIXED / NOT FIXED
        theFixedPolicy.timeshare = !inIsFixed;
        thread_policy_set (pthread_mach_thread_np(threadToAffect), THREAD_EXTENDED_POLICY, (thread_policy_t)&theFixedPolicy, THREAD_EXTENDED_POLICY_COUNT);

        // [2] SET PRECEDENCE
        relativePriority = inPriority;
        thePrecedencePolicy.importance = relativePriority;
        thread_policy_set (pthread_mach_thread_np(threadToAffect), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&thePrecedencePolicy, THREAD_PRECEDENCE_POLICY_COUNT);
    }
}

#else

void setThreadPriority(pthread_t inThread, uint32_t inPriority, bool inIsFixed)
{
    int policy;
    struct sched_param param;

    pthread_getschedparam(inThread, &policy, &param);
    param.sched_priority = sched_get_priority_max(policy);
    pthread_setschedparam(inThread, policy, &param);
}

#endif
