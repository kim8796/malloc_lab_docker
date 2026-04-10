/*
 * ftimer.h - ftimer.c의 공개 인터페이스.
 *
 * Unix 타이머를 이용해 함수 f(argp)의 실행 시간을 초 단위로 측정하는
 * 두 가지 방법을 제공한다. 두 함수 모두 f를 n회 실행하여 평균을 반환한다.
 *
 * [측정 방법 선택]
 * config.h의 USE_ITIMER / USE_GETTOD 플래그로 fsecs.c에서 선택한다.
 * 일반적으로 USE_GETTOD(gettimeofday)가 이식성이 높아 기본값으로 사용된다.
 */

/* 측정 대상 함수 시그니처: void* 인자 하나를 받고 반환값 없음 */
typedef void (*ftimer_test_funct)(void *);

/*
 * ftimer_itimer - ITIMER_REAL(Unix 인터벌 타이머)로 f(argp) 실행 시간 측정.
 *
 * init_etime()으로 타이머를 초기화하고, get_etime() 차이로 경과 시간을 계산한다.
 * n회 평균 실행 시간(초)을 반환한다.
 */
double ftimer_itimer(ftimer_test_funct f, void *argp, int n);

/*
 * ftimer_gettod - gettimeofday()로 f(argp) 실행 시간 측정.
 *
 * f(argp) n회 실행 전후의 gettimeofday() 값 차이로 경과 시간을 계산한다.
 * n회 평균 실행 시간(초)을 반환한다.
 */
double ftimer_gettod(ftimer_test_funct f, void *argp, int n);

