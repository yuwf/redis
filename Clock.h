#ifndef _CLOCK_H_
#define _CLOCK_H_

// by yuwf qingting.water@gmail.com

// 时钟

#if defined(_MSC_VER)
#include <intrin.h>  
#else
#include <x86intrin.h>
#endif

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define	FORCE_INLINE inline __attribute__((always_inline))
#endif

// 获取CPU当前频率值
FORCE_INLINE int64_t TSC()
{
	// rdtsc
	return (int64_t)__rdtsc();

	// rdtscp
	//uint32_t aux;
	//return (int64_t)__rdtscp(&aux);
}

// 获取CPU妙的频率
extern int64_t TSCPerS();
// 获取CPU毫妙的频率
extern int64_t TSCPerMS();
// 获取CPU微妙的频率
extern int64_t TSCPerUS();


#endif