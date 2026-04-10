#include <unistd.h>

/* memlib.c에서 제공하는 메모리 시스템 시뮬레이터 함수 선언 */

void mem_init(void);            /* 가상 힙 초기화 (프로그램 시작 시 1회 호출) */
void mem_deinit(void);          /* 가상 힙 메모리 해제 (프로그램 종료 시 호출) */
void *mem_sbrk(int incr);       /* 힙을 incr 바이트만큼 확장, 이전 brk 반환 */
void mem_reset_brk(void);       /* brk를 초기화해 힙을 빈 상태로 되돌림 */
void *mem_heap_lo(void);        /* 힙의 첫 번째 바이트 주소 반환 */
void *mem_heap_hi(void);        /* 힙의 마지막 유효 바이트 주소 반환 (brk - 1) */
size_t mem_heapsize(void);      /* 현재 사용 중인 힙 크기(바이트) 반환 */
size_t mem_pagesize(void);      /* 시스템 페이지 크기(보통 4096바이트) 반환 */

