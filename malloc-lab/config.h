#ifndef __CONFIG_H_
#define __CONFIG_H_

/*
 * config.h - malloc lab 설정 파일
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * 허가 없이 사용, 수정, 복사 금지.
 */

/*
 * TRACEDIR: 드라이버가 기본 트레이스 파일을 찾는 경로.
 * 실행 시 -t 옵션으로 이 경로를 재정의할 수 있다.
 */
#define TRACEDIR "./traces/"

/*
 * DEFAULT_TRACEFILES: 드라이버가 기본으로 사용할 트레이스 파일 목록.
 * realloc 테스트를 제외하려면 마지막 두 항목을 삭제하면 된다.
 *
 * [트레이스 파일 종류별 의미]
 * amptjp-bal.rep   : 일반적인 할당/해제 패턴
 * cccp-bal.rep     : C 컴파일러 스타일 할당 패턴
 * cp-decl-bal.rep  : C 선언 파서 스타일 패턴
 * expr-bal.rep     : 수식 파서 스타일 패턴
 * coalescing-bal.rep: 연속 해제 후 재할당 (coalescing 성능 테스트)
 * random-bal.rep   : 무작위 크기 할당/해제
 * random2-bal.rep  : 또 다른 무작위 패턴
 * binary-bal.rep   : 두 크기의 블록을 번갈아 할당 (단편화 테스트)
 * binary2-bal.rep  : binary-bal.rep 변형
 * realloc-bal.rep  : realloc 집중 테스트
 * realloc2-bal.rep : realloc 변형 테스트
 */
#define DEFAULT_TRACEFILES \
  "amptjp-bal.rep",\
  "cccp-bal.rep",\
  "cp-decl-bal.rep",\
  "expr-bal.rep",\
  "coalescing-bal.rep",\
  "random-bal.rep",\
  "random2-bal.rep",\
  "binary-bal.rep",\
  "binary2-bal.rep",\
  "realloc-bal.rep",\
  "realloc2-bal.rep"

/*
 * AVG_LIBC_THRUPUT: 기준 시스템에서 libc malloc의 평균 처리량 (연산/초).
 *
 * [역할]
 * 학생 malloc의 처리량이 이 값을 초과하면 처리량 점수는 만점이 된다.
 * 이를 통해 극단적으로 빠르지만 메모리 효율이 낮은 구현을 억제한다.
 */
#define AVG_LIBC_THRUPUT      600E3  /* 600 Kops/sec */

/*
 * UTIL_WEIGHT: 성능 지수에서 공간 이용률의 가중치.
 *
 * [성능 지수 계산]
 * perfindex = UTIL_WEIGHT * util + (1 - UTIL_WEIGHT) * min(thruput / AVG_LIBC_THRUPUT, 1.0)
 * 현재 설정: 공간 이용률 60% + 처리량 40%
 */
#define UTIL_WEIGHT .60

/*
 * ALIGNMENT: 바이트 단위 주소 정렬 요구사항 (4 또는 8).
 * 현재 8바이트(더블 워드) 정렬을 사용한다.
 * mm_malloc이 반환하는 주소는 반드시 이 값의 배수여야 한다.
 */
#define ALIGNMENT 8

/*
 * MAX_HEAP: 최대 힙 크기 (바이트).
 * memlib.c가 시뮬레이션할 가상 힙의 최대 크기이다.
 * 현재 20MB로 설정되어 있다.
 */
#define MAX_HEAP (20*(1<<20))  /* 20 MB */

/*****************************************************************************
 * 시간 측정 방식 선택 (아래 중 정확히 하나만 1로 설정)
 * USE_FCYC   : CPU 사이클 카운터 기반 (x86, Alpha 전용)
 * USE_ITIMER : 인터벌 타이머 기반 (모든 Unix)
 * USE_GETTOD : gettimeofday() 기반 (모든 Unix, 현재 기본값)
 *****************************************************************************/
#define USE_FCYC   0   /* CPU 사이클 카운터, K-best 방식 (x86 & Alpha 전용) */
#define USE_ITIMER 0   /* 인터벌 타이머 (모든 Unix) */
#define USE_GETTOD 1   /* gettimeofday (모든 Unix) - 현재 활성화 */

#endif /* __CONFIG_H */
