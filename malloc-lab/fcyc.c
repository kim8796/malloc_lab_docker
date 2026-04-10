/*
 * fcyc.c - 함수 f의 실행 시간을 CPU 사이클 단위로 추정하는 모듈.
 *
 * [전체 동작 원리: K-best 측정 방식]
 * 단순히 한 번 측정하면 OS 스케줄러, 캐시 미스, 인터럽트 등에 의해
 * 측정값이 튀는 경우가 많다. 이를 방지하기 위해 "K-best" 방식을 사용한다.
 *
 * 1. 함수를 최대 MAXSAMPLES(20)번 반복 실행하며 사이클을 측정한다.
 * 2. 가장 작은 K(3)개의 측정값을 정렬된 상태로 유지한다.
 * 3. K개의 최솟값이 서로 EPSILON(1%) 이내로 수렴하면 측정을 종료한다.
 * 4. 최솟값(values[0])을 최종 결과로 반환한다.
 *
 * [측정 정확도 향상 옵션]
 * - compensate=1: 타이머 인터럽트 오버헤드 보정 (clock.c의 get_comp_counter 사용)
 * - clear_cache=1: 매 측정 전 캐시를 비워 캐시 효과 제거
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * 허가 없이 사용, 수정, 복사 금지.
 */
#include <stdlib.h>
#include <sys/times.h>
#include <stdio.h>

#include "fcyc.h"
#include "clock.h"

/* 기본 파라미터 설정 */
#define K 3                  /* K-best에서 유지할 최솟값 개수 */
#define MAXSAMPLES 20        /* 수렴하지 않을 경우 포기할 최대 측정 횟수 */
#define EPSILON 0.01         /* K개 값이 수렴했다고 판단하는 상대 오차 (1%) */
#define COMPENSATE 0         /* 1이면 타이머 인터럽트 오버헤드 보정 활성화 */
#define CLEAR_CACHE 0        /* 1이면 매 측정 전 캐시 비우기 활성화 */
#define CACHE_BYTES (1<<19)  /* 캐시를 비울 때 사용할 버퍼 크기 (512KB) */
#define CACHE_BLOCK 32       /* 캐시 블록 크기 (바이트) */

/* 실행 중 변경 가능한 파라미터 변수들 */
static int kbest = K;
static int maxsamples = MAXSAMPLES;
static double epsilon = EPSILON;
static int compensate = COMPENSATE;
static int clear_cache = CLEAR_CACHE;
static int cache_bytes = CACHE_BYTES;
static int cache_block = CACHE_BLOCK;

/* 캐시 비우기용 버퍼 (clear_cache=1 일 때 사용) */
static int *cache_buf = NULL;

/* K-best 측정값 배열 및 샘플 카운터 */
static double *values = NULL;  /* 현재까지의 K개 최솟값 (오름차순 정렬) */
static int samplecount = 0;    /* 총 측정 횟수 */

/* 디버깅용 옵션 (0: 비활성화) */
#define KEEP_VALS 0    /* 1이면 values 배열을 fcyc() 반환 후에도 해제하지 않음 */
#define KEEP_SAMPLES 0 /* 1이면 모든 측정값을 samples 배열에 저장 */

#if KEEP_SAMPLES
static double *samples = NULL; /* 모든 측정값 저장 배열 (디버깅용) */
#endif

/*
 * init_sampler - 새로운 샘플링 과정을 시작한다.
 *
 * [동작 방식]
 * 이전 values 배열을 해제하고 kbest 크기의 새 배열을 할당한다.
 * samplecount를 0으로 리셋한다.
 */
static void init_sampler()
{
    if (values)
	free(values);
    values = calloc(kbest, sizeof(double));
#if KEEP_SAMPLES
    if (samples)
	free(samples);
    /* 래핑 분석을 위해 여유분 추가 할당 */
    samples = calloc(maxsamples+kbest, sizeof(double));
#endif
    samplecount = 0;
}

/*
 * add_sample - 새 측정값 val을 K-best 목록에 추가한다.
 *
 * [동작 방식]
 * values 배열은 항상 오름차순으로 정렬된 K개의 최솟값을 유지한다.
 * - samplecount < kbest: 아직 K개 미만이므로 무조건 추가
 * - val < values[kbest-1]: 현재 K번째 최솟값보다 작으면 교체
 * - 추가 후 삽입 정렬로 오름차순 유지
 */
static void add_sample(double val)
{
    int pos = 0;
    if (samplecount < kbest) {
	pos = samplecount;          /* 빈 자리에 삽입 */
	values[pos] = val;
    } else if (val < values[kbest-1]) {
	pos = kbest-1;              /* 가장 큰 값을 새 값으로 교체 */
	values[pos] = val;
    }
#if KEEP_SAMPLES
    samples[samplecount] = val;
#endif
    samplecount++;
    /* 삽입 정렬: 삽입된 위치에서 앞으로 이동하며 정렬 유지 */
    while (pos > 0 && values[pos-1] > values[pos]) {
	double temp = values[pos-1];
	values[pos-1] = values[pos];
	values[pos] = temp;
	pos--;
    }
}

/*
 * has_converged - K개의 최솟값이 epsilon 오차 내로 수렴했는지 확인한다.
 *
 * [수렴 조건]
 * samplecount >= kbest AND values[kbest-1] <= (1 + epsilon) * values[0]
 * 즉, K번째 최솟값이 최솟값의 (1 + 1%) 이하이면 수렴으로 판단한다.
 *
 * 반환값: 수렴했으면 1, 아직 아니면 0
 */
static int has_converged()
{
    return
	(samplecount >= kbest) &&
	((1 + epsilon)*values[0] >= values[kbest-1]);
}

/*
 * clear - 캐시를 비운다.
 *
 * [동작 방식]
 * CACHE_BYTES 크기의 버퍼를 CACHE_BLOCK 간격으로 읽어
 * 캐시를 다른 데이터로 채워 기존 캐시 내용을 축출한다.
 * volatile sink 변수를 사용해 컴파일러가 코드를 최적화(제거)하지 못하게 한다.
 */
static volatile int sink = 0;

static void clear()
{
    int x = sink;
    int *cptr, *cend;
    int incr = cache_block/sizeof(int); /* 블록 크기만큼 건너뛰며 읽기 */
    if (!cache_buf) {
	cache_buf = malloc(cache_bytes);
	if (!cache_buf) {
	    fprintf(stderr, "Fatal error.  Malloc returned null when trying to clear cache\n");
	    exit(1);
	}
    }
    cptr = (int *) cache_buf;
    cend = cptr + cache_bytes/sizeof(int);
    while (cptr < cend) {
	x += *cptr;          /* 읽기 연산으로 캐시 라인 로드 (기존 내용 축출) */
	cptr += incr;
    }
    sink = x; /* volatile 변수에 저장해 최적화 방지 */
}

/*
 * fcyc - K-best 방식으로 함수 f의 실행 시간(사이클)을 추정한다.
 *
 * [동작 흐름]
 * 1. init_sampler()로 초기화
 * 2. 루프:
 *    a. (옵션) clear()로 캐시 비우기
 *    b. start_counter() 또는 start_comp_counter()
 *    c. f(argp) 실행
 *    d. get_counter() 또는 get_comp_counter()로 사이클 측정
 *    e. add_sample()로 K-best 갱신
 *    f. has_converged() 또는 samplecount >= maxsamples 이면 종료
 * 3. values[0](가장 작은 측정값) 반환
 *
 * 반환값: 추정된 실행 사이클 수 (double)
 */
double fcyc(test_funct f, void *argp)
{
    double result;
    init_sampler();
    if (compensate) {
	/* 타이머 인터럽트 보정 모드: start_comp_counter/get_comp_counter 사용 */
	do {
	    double cyc;
	    if (clear_cache)
		clear();
	    start_comp_counter();
	    f(argp);
	    cyc = get_comp_counter();
	    add_sample(cyc);
	} while (!has_converged() && samplecount < maxsamples);
    } else {
	/* 일반 모드: start_counter/get_counter 사용 */
	do {
	    double cyc;
	    if (clear_cache)
		clear();
	    start_counter();
	    f(argp);
	    cyc = get_counter();
	    add_sample(cyc);
	} while (!has_converged() && samplecount < maxsamples);
    }
#ifdef DEBUG
    {
	int i;
	printf(" %d smallest values: [", kbest);
	for (i = 0; i < kbest; i++)
	    printf("%.0f%s", values[i], i==kbest-1 ? "]\n" : ", ");
    }
#endif
    result = values[0]; /* K-best 중 최솟값이 최종 결과 */
#if !KEEP_VALS
    free(values);
    values = NULL;
#endif
    return result;
}


/*************************************************************
 * 측정 파라미터 설정 함수들
 * mdriver.c의 init_fsecs()에서 호출하거나, 사용자가 직접 조정할 수 있다.
 ************************************************************/

/*
 * set_fcyc_clear_cache - 매 측정 전 캐시를 비울지 여부를 설정한다.
 * 활성화하면 캐시 워밍업 효과를 제거해 더 일관된 측정값을 얻는다.
 * 기본값: 0 (비활성화)
 */
void set_fcyc_clear_cache(int clear)
{
    clear_cache = clear;
}

/*
 * set_fcyc_cache_size - 캐시를 비울 때 사용할 버퍼 크기를 설정한다.
 * 측정 대상 시스템의 캐시 크기보다 크게 설정해야 효과적이다.
 * 기본값: 1<<19 (512KB)
 */
void set_fcyc_cache_size(int bytes)
{
    if (bytes != cache_bytes) {
	cache_bytes = bytes;
	if (cache_buf) {
	    free(cache_buf); /* 크기가 바뀌면 기존 버퍼 해제 후 재할당 */
	    cache_buf = NULL;
	}
    }
}

/*
 * set_fcyc_cache_block - 캐시 블록 크기를 설정한다.
 * clear()에서 이 간격으로 버퍼를 읽어 캐시 라인을 축출한다.
 * 기본값: 32바이트
 */
void set_fcyc_cache_block(int bytes) {
    cache_block = bytes;
}

/*
 * set_fcyc_compensate - 타이머 인터럽트 오버헤드 보정 여부를 설정한다.
 * 활성화하면 clock.c의 callibrate()로 캘리브레이션 후 보정한다.
 * 기본값: 0 (비활성화)
 */
void set_fcyc_compensate(int compensate_arg)
{
    compensate = compensate_arg;
}

/*
 * set_fcyc_k - K-best 방식에서 유지할 최솟값 개수를 설정한다.
 * K가 클수록 수렴 기준이 엄격해져 정확도는 높아지지만 더 오래 걸린다.
 * 기본값: 3
 */
void set_fcyc_k(int k)
{
    kbest = k;
}

/*
 * set_fcyc_maxsamples - K-best 수렴 실패 시 포기할 최대 측정 횟수를 설정한다.
 * 이 횟수를 초과하면 수렴 여부와 관계없이 지금까지의 최솟값을 반환한다.
 * 기본값: 20
 */
void set_fcyc_maxsamples(int maxsamples_arg)
{
    maxsamples = maxsamples_arg;
}

/*
 * set_fcyc_epsilon - K-best 수렴 판단 기준 상대 오차를 설정한다.
 * 값이 작을수록 더 엄격하게 수렴을 판단한다 (더 많은 샘플 필요).
 * 기본값: 0.01 (1%)
 */
void set_fcyc_epsilon(double epsilon_arg)
{
    epsilon = epsilon_arg;
}

