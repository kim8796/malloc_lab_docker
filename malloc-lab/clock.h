/* clock.c에서 제공하는 CPU 사이클 카운터 함수 선언 */

/* 사이클 카운터 시작 (측정 시작 시점 저장) */
void start_counter();

/* start_counter() 호출 이후 경과한 사이클 수 반환 */
double get_counter();

/* 카운터 호출 자체의 오버헤드(사이클 수) 측정 */
double ovhd();

/* CPU 클럭 속도 측정 (기본 sleeptime 사용, MHz 단위 반환) */
double mhz(int verbose);

/* CPU 클럭 속도 측정 (sleeptime 직접 지정, MHz 단위 반환) */
double mhz_full(int verbose, int sleeptime);

/*
 * 타이머 인터럽트 오버헤드를 보정하는 특수 카운터.
 * 일반 카운터보다 정확하지만 캘리브레이션 비용이 있다.
 */

/* 보정 카운터 시작 (최초 호출 시 callibrate() 자동 실행) */
void start_comp_counter();

/* 타이머 인터럽트 오버헤드 제거 후 경과 사이클 수 반환 */
double get_comp_counter();
