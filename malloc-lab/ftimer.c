/*
 * ftimer.c - Unix 타이머를 이용해 함수 실행 시간(초)을 측정하는 모듈.
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * 허가 없이 사용, 수정, 복사 불가.
 *
 * [제공하는 두 가지 측정 방법]
 * ftimer_itimer: Unix 인터벌 타이머(setitimer/getitimer) 사용
 * ftimer_gettod : gettimeofday() 사용 (현재 config.h에서 기본 선택)
 *
 * [공통 동작]
 * 두 함수 모두 f(argp)를 n회 실행하여 평균 실행 시간(초)을 반환한다.
 * n회 반복으로 측정 오차를 줄이고 평균을 취함으로써 단발성 노이즈를 제거한다.
 */
#include <stdio.h>
#include <sys/time.h>
#include "ftimer.h"

/* 내부 헬퍼 함수: 인터벌 타이머 초기화 및 경과 시간 조회 */
static void init_etime(void);
static double get_etime(void);

/*
 * ftimer_itimer - Unix 인터벌 타이머(ITIMER_REAL)로 f(argp)의 실행 시간을 측정.
 *
 * [동작 방식]
 * 1. init_etime()으로 ITIMER_VIRTUAL/REAL/PROF 세 타이머를 86400초로 초기화
 * 2. get_etime()으로 시작 시각 기록 (ITIMER_REAL 카운트다운 잔여 시간)
 * 3. f(argp)를 n회 반복 실행
 * 4. get_etime()으로 종료 시각 조회 → 경과 시간 계산
 * 5. 경과 시간 / n = 회당 평균 실행 시간(초) 반환
 *
 * 인자: f - 측정 대상 함수, argp - 인자 포인터, n - 반복 횟수
 * 반환: f(argp) 1회 평균 실행 시간 (초)
 */
double ftimer_itimer(ftimer_test_funct f, void *argp, int n)
{
    double start, tmeas;
    int i;

    init_etime();          /* 타이머 초기화 */
    start = get_etime();   /* 시작 시각 기록 */
    for (i = 0; i < n; i++)
	f(argp);           /* 측정 대상 함수 n회 실행 */
    tmeas = get_etime() - start; /* 총 경과 시간 */
    return tmeas / n;            /* n회 평균 반환 */
}

/*
 * ftimer_gettod - gettimeofday()로 f(argp)의 실행 시간을 측정.
 *
 * [동작 방식]
 * 1. gettimeofday()로 시작 시각(stv) 기록
 * 2. f(argp)를 n회 반복 실행
 * 3. gettimeofday()로 종료 시각(etv) 기록
 * 4. diff = (tv_sec 차이) * 1000ms + (tv_usec 차이) / 1000ms (→ 밀리초 단위)
 * 5. diff / n = 1회 평균 (밀리초), * 1e-3 = 초 단위로 변환 후 반환
 *
 * [gettimeofday 사용 이유]
 * setitimer 방식보다 간단하고, POSIX 표준이며, 대부분의 Linux에서 정확도가 높다.
 * 현재 config.h에서 USE_GETTOD=1로 기본 선택되어 있다.
 *
 * 인자: f - 측정 대상 함수, argp - 인자 포인터, n - 반복 횟수
 * 반환: f(argp) 1회 평균 실행 시간 (초)
 */
double ftimer_gettod(ftimer_test_funct f, void *argp, int n)
{
    int i;
    struct timeval stv, etv;
    double diff;

    gettimeofday(&stv, NULL);   /* 시작 시각 기록 (초 + 마이크로초) */
    for (i = 0; i < n; i++)
	f(argp);                /* 측정 대상 함수 n회 실행 */
    gettimeofday(&etv, NULL);   /* 종료 시각 기록 */

    /*
     * diff 계산:
     *   (etv.tv_sec - stv.tv_sec) * 1000      → 초 차이를 밀리초로 변환
     * + (etv.tv_usec - stv.tv_usec) * 0.001   → 마이크로초 차이를 밀리초로 변환
     * = 총 경과 시간(밀리초)
     */
    diff = 1E3*(etv.tv_sec - stv.tv_sec) + 1E-3*(etv.tv_usec - stv.tv_usec);
    diff /= n;          /* n회 평균 (밀리초) */
    return (1E-3*diff); /* 밀리초 → 초 변환 후 반환 */
}


/*
 * ================================================================
 * Unix 인터벌 타이머 조작 헬퍼 루틴
 * ================================================================
 *
 * Unix 인터벌 타이머는 카운트다운 방식으로 동작한다.
 * - 초기값(it_value)을 설정하면 커널이 시간이 지날수록 값을 감소시킨다.
 * - it_value가 0이 되면 SIGALRM(REAL), SIGVTALRM(VIRTUAL), SIGPROF(PROF) 시그널 발생
 *
 * init_etime()에서 세 타이머를 모두 MAX_ETIME(86400초=1일)으로 설정하므로
 * 측정 중에 타이머가 만료될 일이 없다. get_etime()은 잔여 시간의 감소량으로
 * 경과 시간을 계산한다.
 *
 * ITIMER_REAL은 실제 경과 시간(wall-clock)을 측정한다.
 * ITIMER_VIRTUAL은 사용자 CPU 시간만 측정한다.
 * ITIMER_PROF는 사용자+커널 CPU 시간을 측정한다.
 *
 * get_etime()은 ITIMER_REAL(first_p → r_curr 비교)로 경과 시간을 반환한다.
 * (변수명이 first_p이지만 실제로는 r_curr와 비교하여 REAL 타이머 기준으로 계산)
 */

/* 인터벌 타이머 초기값: 86400초 (24시간) — 측정 중 만료 방지 */
#define MAX_ETIME 86400

/* 세 타이머의 초기 상태를 저장하는 정적 변수 */
static struct itimerval first_u; /* ITIMER_VIRTUAL 초기값 (사용자 CPU 시간) */
static struct itimerval first_r; /* ITIMER_REAL 초기값 (실제 경과 시간) */
static struct itimerval first_p; /* ITIMER_PROF 초기값 (사용자+커널 CPU 시간) */

/*
 * init_etime - 세 가지 인터벌 타이머를 MAX_ETIME(86400초)으로 초기화한다.
 *
 * [each itimerval 구조체 필드]
 * it_interval: 타이머 만료 후 재시작 간격 (0 = 한 번만 카운트다운)
 * it_value   : 초기 카운트다운 값 (setitimer 이후 커널이 감소시킴)
 *
 * 세 타이머 모두 반복 없이(it_interval=0) 86400초 카운트다운으로 설정한다.
 */
static void init_etime(void)
{
    /* ITIMER_VIRTUAL (사용자 CPU 시간) */
    first_u.it_interval.tv_sec = 0;
    first_u.it_interval.tv_usec = 0;
    first_u.it_value.tv_sec = MAX_ETIME;
    first_u.it_value.tv_usec = 0;
    setitimer(ITIMER_VIRTUAL, &first_u, NULL);

    /* ITIMER_REAL (실제 경과 시간, wall-clock) */
    first_r.it_interval.tv_sec = 0;
    first_r.it_interval.tv_usec = 0;
    first_r.it_value.tv_sec = MAX_ETIME;
    first_r.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &first_r, NULL);

    /* ITIMER_PROF (사용자+커널 CPU 시간) */
    first_p.it_interval.tv_sec = 0;
    first_p.it_interval.tv_usec = 0;
    first_p.it_value.tv_sec = MAX_ETIME;
    first_p.it_value.tv_usec = 0;
    setitimer(ITIMER_PROF, &first_p, NULL);
}

/*
 * get_etime - init_etime() 호출 이후 경과한 실제 시간(초)을 반환한다.
 *
 * [계산 원리]
 * ITIMER_REAL은 카운트다운 방식이므로:
 *   경과 시간 = 초기값(first_p.it_value) - 현재 잔여값(r_curr.it_value)
 *
 * tv_sec 항: 초 단위 차이
 * tv_usec 항: 마이크로초 단위 차이 * 1e-6 → 초 단위로 변환
 *
 * 반환값: 경과 실시간 (초, double)
 *
 * 주의: 변수명 first_p를 사용하지만 r_curr(REAL 타이머)과 비교하므로
 *       실제로는 ITIMER_REAL 기준의 wall-clock 경과 시간을 반환한다.
 */
static double get_etime(void) {
    struct itimerval v_curr; /* VIRTUAL 타이머 현재 잔여값 */
    struct itimerval r_curr; /* REAL 타이머 현재 잔여값 */
    struct itimerval p_curr; /* PROF 타이머 현재 잔여값 */

    getitimer(ITIMER_VIRTUAL, &v_curr);
    getitimer(ITIMER_REAL, &r_curr);
    getitimer(ITIMER_PROF, &p_curr);

    /*
     * 경과 시간 = 초기값 - 현재 잔여값
     * (first_p.it_value - r_curr.it_value) 를 초 단위로 계산
     *
     * tv_sec 차이 + tv_usec 차이 * 1e-6 = 총 경과 초
     */
    return (double) ((first_p.it_value.tv_sec - r_curr.it_value.tv_sec) +
		     (first_p.it_value.tv_usec - r_curr.it_value.tv_usec)*1e-6);
}

