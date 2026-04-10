/*
 * fsecs.c - 함수 실행 시간을 초(seconds) 단위로 측정하는 고수준 래퍼.
 *
 * [역할]
 * mdriver.c는 malloc 패키지의 처리량을 측정할 때 항상 fsecs()를 호출한다.
 * fsecs()는 config.h의 설정(USE_FCYC / USE_ITIMER / USE_GETTOD)에 따라
 * 실제 측정 방법을 아래 세 가지 중 하나로 선택한다.
 *
 * [타이밍 방법 비교]
 * USE_FCYC  : clock.c + fcyc.c의 K-best 사이클 카운터 사용.
 *             가장 정밀하지만 x86/Alpha 전용이며, CPU 주파수 측정 필요.
 *             결과: cycles / (Mhz * 1e6) = 초
 *
 * USE_ITIMER: ftimer.c의 Unix 인터벌 타이머 사용.
 *             모든 Unix에서 동작, 마이크로초 단위 측정.
 *
 * USE_GETTOD: ftimer.c의 gettimeofday() 사용 (현재 기본값).
 *             모든 Unix에서 동작, 구현이 가장 간단하고 이식성이 높음.
 *
 * [init_fsecs()와 fsecs()의 관계]
 * init_fsecs()는 프로그램 시작 시 1회 호출되어 Mhz 측정 등 초기화를 한다.
 * fsecs()는 각 트레이스 측정마다 반복 호출된다.
 */
#include <stdio.h>
#include "fsecs.h"
#include "fcyc.h"
#include "clock.h"
#include "ftimer.h"
#include "config.h"

static double Mhz;  /* 측정된 CPU 클럭 주파수 (USE_FCYC 모드에서만 사용) */

extern int verbose; /* mdriver.c의 -v 옵션 플래그 */

/*
 * init_fsecs - 타이밍 패키지를 초기화한다.
 *
 * [동작 방식]
 * USE_FCYC 모드: fcyc 파라미터 설정 + mhz()로 CPU 주파수 측정 (약 2초 소요)
 * USE_ITIMER / USE_GETTOD 모드: 별도 초기화 불필요, verbose 메시지만 출력
 */
void init_fsecs(void)
{
    Mhz = 0; /* gcc -Wall 경고 억제용 초기화 */

#if USE_FCYC
    if (verbose)
	printf("Measuring performance with a cycle counter.\n");

    /* fcyc.c 파라미터 설정 */
    set_fcyc_maxsamples(20); /* 최대 20회 샘플 */
    set_fcyc_clear_cache(1); /* 매 측정 전 캐시 비우기 활성화 */
    set_fcyc_compensate(1);  /* 타이머 인터럽트 오버헤드 보정 활성화 */
    set_fcyc_epsilon(0.01);  /* 1% 수렴 기준 */
    set_fcyc_k(3);           /* K-best: 3개 최솟값 수렴 확인 */
    Mhz = mhz(verbose > 0); /* CPU 주파수 측정 (사이클→초 변환에 사용) */
#elif USE_ITIMER
    if (verbose)
	printf("Measuring performance with the interval timer.\n");
#elif USE_GETTOD
    if (verbose)
	printf("Measuring performance with gettimeofday().\n");
#endif
}

/*
 * fsecs - 함수 f(argp)의 실행 시간을 초(seconds) 단위로 반환한다.
 *
 * [동작 방식]
 * USE_FCYC  : fcyc(f, argp)로 사이클 수 측정 → cycles / (Mhz * 1e6)으로 초 변환
 * USE_ITIMER: ftimer_itimer(f, argp, 10)으로 10회 평균 실행 시간(초) 반환
 * USE_GETTOD: ftimer_gettod(f, argp, 10)으로 10회 평균 실행 시간(초) 반환
 *
 * 반환값: 함수 f의 추정 실행 시간 (초)
 */
double fsecs(fsecs_test_funct f, void *argp)
{
#if USE_FCYC
    double cycles = fcyc(f, argp);   /* K-best 사이클 측정 */
    return cycles/(Mhz*1e6);         /* 사이클 / (MHz * 1,000,000) = 초 */
#elif USE_ITIMER
    return ftimer_itimer(f, argp, 10); /* 인터벌 타이머로 10회 평균 */
#elif USE_GETTOD
    return ftimer_gettod(f, argp, 10); /* gettimeofday로 10회 평균 */
#endif
}

