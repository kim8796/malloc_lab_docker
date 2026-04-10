/*
 * fcyc.h - fcyc.c에서 제공하는 K-best 사이클 측정 함수 선언.
 *
 * [K-best 방식 요약]
 * 함수를 최대 MAXSAMPLES번 실행하면서 가장 작은 K개의 측정값을 수집하고,
 * 이 K개가 EPSILON(1%) 이내로 수렴하면 최솟값을 반환한다.
 * 이 방식으로 OS 인터럽트나 캐시 미스에 의한 이상치를 자동으로 제거한다.
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * 허가 없이 사용, 수정, 복사 금지.
 */

/* 테스트 함수 타입: void* 인자 하나를 받고 반환값 없음 */
typedef void (*test_funct)(void *);

/*
 * fcyc - K-best 방식으로 함수 f(argp)의 실행 시간(사이클)을 추정한다.
 * 반환값: 추정된 최소 실행 사이클 수
 */
double fcyc(test_funct f, void* argp);

/*************************************************************
 * 측정 파라미터 설정 함수들 (선택적으로 호출해 동작 조정 가능)
 *************************************************************/

/*
 * set_fcyc_clear_cache - 매 측정 전 캐시를 비울지 여부 설정.
 * 비우면 캐시 워밍업 효과가 없어지고 측정값이 더 일관적이 됨.
 * 기본값: 0 (비활성화)
 */
void set_fcyc_clear_cache(int clear);

/*
 * set_fcyc_cache_size - 캐시를 비울 때 사용할 버퍼 크기 설정 (바이트).
 * 시스템 캐시 크기보다 크게 설정해야 효과적임.
 * 기본값: 1<<19 (512KB)
 */
void set_fcyc_cache_size(int bytes);

/*
 * set_fcyc_cache_block - 캐시 블록 크기 설정 (바이트).
 * 캐시 비우기 루프에서 이 간격으로 버퍼를 읽어 캐시 라인 축출.
 * 기본값: 32
 */
void set_fcyc_cache_block(int bytes);

/*
 * set_fcyc_compensate - 타이머 인터럽트 오버헤드 보정 여부 설정.
 * 활성화 시 clock.c의 callibrate()로 보정값을 계산해 차감.
 * 기본값: 0 (비활성화)
 */
void set_fcyc_compensate(int compensate_arg);

/*
 * set_fcyc_k - K-best에서 유지할 최솟값 개수 설정.
 * K가 클수록 수렴 기준이 엄격해져 정확도 향상, 측정 시간 증가.
 * 기본값: 3
 */
void set_fcyc_k(int k);

/*
 * set_fcyc_maxsamples - 수렴 실패 시 포기할 최대 측정 횟수 설정.
 * 이 횟수를 초과하면 지금까지의 최솟값을 반환.
 * 기본값: 20
 */
void set_fcyc_maxsamples(int maxsamples_arg);

/*
 * set_fcyc_epsilon - K-best 수렴 판단 상대 오차 설정.
 * 값이 작을수록 더 엄격하게 수렴을 판단 (더 많은 샘플 필요).
 * 기본값: 0.01 (1%)
 */
void set_fcyc_epsilon(double epsilon_arg);


