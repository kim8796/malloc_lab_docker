/*
 * fsecs.h - fsecs.c의 공개 인터페이스.
 *
 * fsecs는 config.h의 USE_FCYC / USE_ITIMER / USE_GETTOD 플래그에 따라
 * 적절한 타이밍 백엔드(fcyc / ftimer_itimer / ftimer_gettod)를 선택하여
 * 함수 실행 시간을 초(seconds) 단위로 반환하는 고수준 래퍼다.
 */

/* 측정 대상 함수의 시그니처: void 포인터 인자 하나를 받고 반환값 없음 */
typedef void (*fsecs_test_funct)(void *);

/* init_fsecs - 타이밍 패키지 초기화 (프로그램 시작 시 1회 호출) */
void init_fsecs(void);

/* fsecs - 함수 f(argp)의 실행 시간을 초 단위로 반환 */
double fsecs(fsecs_test_funct f, void *argp);
