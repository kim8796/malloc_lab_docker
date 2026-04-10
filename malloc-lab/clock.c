/*
 * clock.c - x86, Alpha, Sparc 아키텍처에서 CPU 사이클 카운터를 사용하는 루틴 모음.
 *
 * [전체 동작 원리]
 * CPU 내부에는 전원 투입 이후 매 클럭마다 1씩 증가하는 64비트 카운터(TSC: Time Stamp Counter)가 있다.
 * 이 파일의 함수들은 이 카운터를 읽어 두 시점 사이의 경과 사이클 수를 측정한다.
 *
 * [사용 흐름]
 * 1. start_counter() → 현재 카운터 값을 저장
 * 2. (측정할 코드 실행)
 * 3. get_counter()   → 저장된 값과의 차이(사이클 수) 반환
 *
 * [아키텍처별 구현]
 * - x86(i386): rdtsc 명령어로 TSC 읽기 (인라인 어셈블리)
 * - Alpha:     rpcc 명령어 사용 (머신 코드 배열로 직접 구현)
 * - 그 외:     미구현 (config.h에서 USE_GETTOD 또는 USE_ITIMER 선택 필요)
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * 허가 없이 사용, 수정, 복사 금지.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/times.h>
#include "clock.h"


/*******************************************************
 * 아키텍처 의존 함수들
 *
 * 참고: __i386__, __alpha 상수는 GCC가 전처리기 호출 시 자동으로 설정한다.
 * 'gcc -v'로 확인 가능.
 *******************************************************/

#if defined(__i386__)
/*******************************************************
 * Pentium(x86) 버전 start_counter() / get_counter()
 *
 * [동작 방식]
 * rdtsc(Read Time-Stamp Counter) 명령어는 64비트 TSC 값을
 * EDX(상위 32비트) : EAX(하위 32비트) 레지스터 쌍에 저장한다.
 * 이를 읽어 전역변수 cyc_hi, cyc_lo에 보관하고,
 * get_counter()에서 double 정밀도 뺄셈으로 경과 사이클을 계산한다.
 *******************************************************/

/* 사이클 카운터의 시작 값 (상위/하위 32비트로 분리 저장) */
static unsigned cyc_hi = 0;
static unsigned cyc_lo = 0;


/*
 * access_counter - rdtsc 명령어로 현재 TSC 값을 읽어 *hi, *lo에 저장한다.
 *
 * [인라인 어셈블리 설명]
 * "rdtsc"        : EDX:EAX에 TSC 값 저장
 * "movl %%edx,%0": EDX → *hi
 * "movl %%eax,%1": EAX → *lo
 * "%edx", "%eax" : clobber list (이 레지스터들이 변경됨을 GCC에 알림)
 */
void access_counter(unsigned *hi, unsigned *lo)
{
    asm("rdtsc; movl %%edx,%0; movl %%eax,%1"   /* TSC 읽기 */
	: "=r" (*hi), "=r" (*lo)                /* 결과를 두 출력 변수에 저장 */
	: /* 입력 없음 */
	: "%edx", "%eax");
}

/*
 * start_counter - 현재 사이클 카운터 값을 전역 변수에 저장한다.
 * 측정 시작 전에 호출한다.
 */
void start_counter()
{
    access_counter(&cyc_hi, &cyc_lo);
}

/*
 * get_counter - start_counter() 호출 이후 경과한 사이클 수를 반환한다.
 *
 * [계산 방법]
 * 64비트 TSC를 두 개의 unsigned 32비트 값으로 관리하므로,
 * borrow(빌림수)를 처리하는 더블 정밀도 뺄셈을 사용한다.
 * result = hi * 2^32 + lo (double로 변환)
 *
 * 반환값: 경과 사이클 수 (double)
 */
double get_counter()
{
    unsigned ncyc_hi, ncyc_lo;
    unsigned hi, lo, borrow;
    double result;

    /* 현재 카운터 값 읽기 */
    access_counter(&ncyc_hi, &ncyc_lo);

    /* 64비트 뺄셈: 하위 32비트에서 빌림 처리 */
    lo = ncyc_lo - cyc_lo;
    borrow = lo > ncyc_lo;    /* 언더플로우 발생 시 borrow = 1 */
    hi = ncyc_hi - cyc_hi - borrow;
    result = (double) hi * (1 << 30) * 4 + lo; /* hi * 2^32 + lo */
    if (result < 0) {
	fprintf(stderr, "Error: counter returns neg value: %.0f\n", result);
    }
    return result;
}

#elif defined(__alpha)

/****************************************************
 * Alpha 버전 start_counter() / get_counter()
 *
 * [동작 방식]
 * rpcc(Read Process Cycle Counter) 명령어로 64비트 레지스터를 읽는다.
 * 하위 32비트: 현재 프로세스의 사이클 카운터
 * 상위 32비트: 벽시계(wall clock) 사이클
 * counterRoutine 배열에 직접 Alpha 기계어 코드를 넣어 함수로 캐스팅한다.
 *
 * 주의: 450MHz 클럭 기준 약 9초까지만 측정 가능 (32비트 범위 한계)
 ***************************************************/

/* 사이클 카운터 시작 값 */
static unsigned cyc_hi = 0;
static unsigned cyc_lo = 0;

/*
 * counterRoutine - rpcc 명령어를 담은 Alpha 기계어 코드 배열.
 * 이 배열을 함수 포인터로 캐스팅해 직접 실행한다.
 */
static unsigned int counterRoutine[] =
{
    0x601fc000u,
    0x401f0000u,
    0x6bfa8001u
};

/* 위 기계어 코드를 "unsigned int를 반환하는 함수"로 캐스팅 */
static unsigned int (*counter)(void)= (void *)counterRoutine;


void start_counter()
{
    cyc_hi = 0;
    cyc_lo = counter(); /* rpcc로 현재 사이클 값 읽기 */
}

double get_counter()
{
    unsigned ncyc_hi, ncyc_lo;
    unsigned hi, lo, borrow;
    double result;
    ncyc_lo = counter();
    ncyc_hi = 0;
    lo = ncyc_lo - cyc_lo;
    borrow = lo > ncyc_lo;
    hi = ncyc_hi - cyc_hi - borrow;
    result = (double) hi * (1 << 30) * 4 + lo;
    if (result < 0) {
	fprintf(stderr, "Error: Cycle counter returning negative value: %.0f\n", result);
    }
    return result;
}

#else

/****************************************************************
 * 미지원 플랫폼용 스텁(stub) 함수
 *
 * Sparc v8plus 이상은 사이클 카운터를 지원하지만,
 * 구형 Sparc 호환성을 위해 구현하지 않았다.
 * 이 플랫폼에서는 config.h에서 USE_GETTOD 또는 USE_ITIMER를 선택해야 한다.
 ***************************************************************/

void start_counter()
{
    printf("ERROR: You are trying to use a start_counter routine in clock.c\n");
    printf("that has not been implemented yet on this platform.\n");
    printf("Please choose another timing package in config.h.\n");
    exit(1);
}

double get_counter()
{
    printf("ERROR: You are trying to use a get_counter routine in clock.c\n");
    printf("that has not been implemented yet on this platform.\n");
    printf("Please choose another timing package in config.h.\n");
    exit(1);
}
#endif




/*******************************
 * 아키텍처 독립 함수들
 ******************************/

/*
 * ovhd - 사이클 카운터 자체의 오버헤드(측정 비용)를 측정한다.
 *
 * [동작 방식]
 * start_counter() → get_counter()를 2회 반복하여 캐시 효과를 제거하고,
 * 카운터 호출만으로 소요되는 사이클 수를 반환한다.
 * fcyc.c의 측정값 보정에 활용할 수 있다.
 */
double ovhd()
{
    int i;
    double result;

    /* 캐시 효과 제거를 위해 2회 반복 */
    for (i = 0; i < 2; i++) {
	start_counter();
	result = get_counter();
    }
    return result;
}

/*
 * mhz_full - CPU 클럭 속도(MHz)를 측정한다.
 *
 * [측정 방법]
 * 1. start_counter()로 사이클 카운터 시작
 * 2. sleeptime초 동안 sleep()
 * 3. get_counter()로 경과 사이클 수 측정
 * 4. rate = 경과 사이클 / (sleeptime * 1e6) → MHz 단위
 *
 * sleeptime이 길수록 정확하지만 느리다.
 *
 * 반환값: CPU 클럭 속도 (MHz)
 */
double mhz_full(int verbose, int sleeptime)
{
    double rate;

    start_counter();
    sleep(sleeptime);
    rate = get_counter() / (1e6*sleeptime);
    if (verbose)
	printf("Processor clock rate ~= %.1f MHz\n", rate);
    return rate;
}

/*
 * mhz - 기본 sleeptime(2초)을 사용하여 CPU 클럭 속도를 측정한다.
 */
double mhz(int verbose)
{
    return mhz_full(verbose, 2);
}

/** 타이머 인터럽트 오버헤드를 보정하는 특수 카운터 **/

/*
 * cyc_per_tick - 타이머 인터럽트 1틱당 사이클 수 (캘리브레이션으로 측정).
 * callibrate()가 한 번 측정하면 이후 재측정하지 않는다.
 */
static double cyc_per_tick = 0.0;

/* callibrate()에서 사용하는 상수들 */
#define NEVENT 100       /* 수집할 이벤트(타이머 틱) 수 */
#define THRESHOLD 1000   /* 새 이벤트로 인정할 최소 경과 사이클 */
#define RECORDTHRESH 3000 /* 유효한 cyc_per_tick 최솟값 */

/*
 * callibrate - 타이머 인터럽트 1틱당 사이클 수(cyc_per_tick)를 측정한다.
 *
 * [동작 방식]
 * NEVENT번의 타이머 틱을 관찰하면서, 각 틱 사이에 경과한 사이클 수를
 * 틱 수로 나누어 cyc_per_tick을 추정한다.
 * 가장 작은 값을 채택하여 인터럽트 오버헤드 추정치를 최소화한다.
 */
static void callibrate(int verbose)
{
    double oldt;
    struct tms t;
    clock_t oldc;
    int e = 0;

    times(&t);
    oldc = t.tms_utime;
    start_counter();
    oldt = get_counter();
    while (e <NEVENT) {
	double newt = get_counter();

	if (newt-oldt >= THRESHOLD) { /* THRESHOLD 사이클 이상 경과 시 틱 이벤트 감지 */
	    clock_t newc;
	    times(&t);
	    newc = t.tms_utime;
	    if (newc > oldc) { /* 실제로 틱이 발생했으면 */
		double cpt = (newt-oldt)/(newc-oldc); /* 이 구간의 사이클/틱 */
		if ((cyc_per_tick == 0.0 || cyc_per_tick > cpt) && cpt > RECORDTHRESH)
		    cyc_per_tick = cpt; /* 더 작은 값으로 갱신 (최솟값 추적) */
		e++;
		oldc = newc;
	    }
	    oldt = newt;
	}
    }
    if (verbose)
	printf("Setting cyc_per_tick to %f\n", cyc_per_tick);
}

static clock_t start_tick = 0; /* start_comp_counter() 호출 시점의 틱 값 */

/*
 * start_comp_counter - 타이머 인터럽트 보정 카운터를 시작한다.
 *
 * [동작 방식]
 * cyc_per_tick이 아직 측정되지 않았으면 먼저 callibrate()를 호출한다.
 * 이후 현재 틱 값과 사이클 카운터를 함께 저장한다.
 */
void start_comp_counter()
{
    struct tms t;

    if (cyc_per_tick == 0.0)
	callibrate(0); /* 최초 1회만 캘리브레이션 */
    times(&t);
    start_tick = t.tms_utime;
    start_counter();
}

/*
 * get_comp_counter - 타이머 인터럽트 오버헤드를 제거한 경과 사이클 수를 반환한다.
 *
 * [동작 방식]
 * 측정된 총 사이클에서 (경과 틱 수 × cyc_per_tick)을 빼서
 * 타이머 인터럽트에 의한 사이클 낭비를 제거한다.
 *
 * 반환값: 인터럽트 오버헤드 제거 후 순수 사이클 수
 */
double get_comp_counter()
{
    double time = get_counter(); /* 총 경과 사이클 */
    double ctime;
    struct tms t;
    clock_t ticks;

    times(&t);
    ticks = t.tms_utime - start_tick;          /* 경과 틱 수 */
    ctime = time - ticks*cyc_per_tick;          /* 인터럽트 오버헤드 제거 */
    return ctime;
}

