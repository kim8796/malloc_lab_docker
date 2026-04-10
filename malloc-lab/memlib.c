/*
 * memlib.c - 메모리 시스템을 시뮬레이션하는 모듈.
 *
 * [존재 이유]
 * 학생이 구현하는 malloc 패키지(mm.c)와 시스템의 libc malloc이
 * 동일한 프로세스 내에서 서로 간섭하지 않도록, 이 모듈은
 * malloc으로 미리 확보한 큰 메모리 영역을 "가상 힙"으로 사용한다.
 *
 * [동작 방식]
 * - mem_init()이 호출되면 malloc(MAX_HEAP)으로 대용량 버퍼를 확보한다.
 * - 이후 mm.c에서 mem_sbrk()를 호출하면 이 버퍼 안에서
 *   brk 포인터(mem_brk)를 앞으로 움직여 힙을 확장하는 것을 흉내낸다.
 * - 실제 OS의 sbrk()와 달리 힙 축소(음수 incr)는 지원하지 않는다.
 *
 * [힙 메모리 구조]
 * mem_start_brk          mem_brk            mem_max_addr
 *     |←─── 사용된 힙 영역 ───→|←── 남은 공간 ──→|
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#include "memlib.h"
#include "config.h"

/* 내부 전용 변수 (외부에서 직접 접근 불가) */
static char *mem_start_brk;  /* 힙의 첫 번째 바이트를 가리키는 포인터 */
static char *mem_brk;        /* 현재 brk 위치 (힙의 끝, 다음 할당 가능 주소) */
static char *mem_max_addr;   /* 힙의 최대 주소 (mem_start_brk + MAX_HEAP) */

/*
 * mem_init - 메모리 시스템 모델을 초기화한다.
 *
 * [동작 방식]
 * 1. malloc(MAX_HEAP)으로 큰 버퍼를 확보하여 가상 힙으로 사용
 * 2. mem_start_brk = 버퍼 시작 주소
 * 3. mem_max_addr  = 버퍼 끝 주소 (mem_start_brk + MAX_HEAP)
 * 4. mem_brk       = mem_start_brk (처음에는 힙이 비어있음)
 */
void mem_init(void)
{
    /* 가상 힙으로 사용할 버퍼 확보 */
    if ((mem_start_brk = (char *)malloc(MAX_HEAP)) == NULL) {
	fprintf(stderr, "mem_init_vm: malloc error\n");
	exit(1);
    }

    mem_max_addr = mem_start_brk + MAX_HEAP;  /* 힙의 최대 법적 주소 설정 */
    mem_brk = mem_start_brk;                  /* 초기에는 힙이 비어있음 */
}

/*
 * mem_deinit - 메모리 시스템 모델에 사용된 저장소를 해제한다.
 *
 * [동작 방식]
 * mem_init()에서 malloc으로 확보한 버퍼를 free()로 반환한다.
 */
void mem_deinit(void)
{
    free(mem_start_brk);
}

/*
 * mem_reset_brk - 시뮬레이션된 brk 포인터를 초기화하여 힙을 비운다.
 *
 * [동작 방식]
 * mem_brk를 mem_start_brk로 되돌려 힙을 다시 비어있는 상태로 만든다.
 * 각 트레이스 파일을 테스트하기 전에 호출되어 힙을 초기화한다.
 */
void mem_reset_brk()
{
    mem_brk = mem_start_brk;
}

/*
 * mem_sbrk - sbrk 시스템 콜을 단순하게 흉내낸 함수.
 *
 * [동작 방식]
 * 1. 현재 mem_brk 위치를 old_brk에 저장
 * 2. incr만큼 mem_brk를 앞으로 이동 (힙 확장)
 * 3. 확장 전의 mem_brk(old_brk)를 반환 → 새로 할당된 영역의 시작 주소
 *
 * [제약사항]
 * - incr < 0이면 힙 축소를 의미하는데, 이 구현에서는 지원하지 않는다.
 * - 확장 후 mem_brk가 mem_max_addr를 초과하면 힙이 꽉 찬 상태이므로 실패한다.
 *
 * 반환값: 확장 전 mem_brk 주소 (새 영역 시작), 실패 시 (void*)-1
 */
void *mem_sbrk(int incr)
{
    char *old_brk = mem_brk;

    if ( (incr < 0) || ((mem_brk + incr) > mem_max_addr)) {
	errno = ENOMEM;
	fprintf(stderr, "ERROR: mem_sbrk failed. Ran out of memory...\n");
	return (void *)-1;
    }
    mem_brk += incr; /* brk 포인터를 incr만큼 앞으로 이동 */
    return (void *)old_brk; /* 새로 할당된 영역의 시작 주소 반환 */
}

/*
 * mem_heap_lo - 힙의 첫 번째 바이트 주소를 반환한다.
 *
 * [용도]
 * mdriver.c의 add_range()에서 블록이 힙 범위 안에 있는지 검사할 때 사용한다.
 */
void *mem_heap_lo()
{
    return (void *)mem_start_brk;
}

/*
 * mem_heap_hi - 힙의 마지막 바이트 주소를 반환한다.
 *
 * [용도]
 * mdriver.c의 add_range()에서 블록이 힙 범위를 벗어나지 않는지 검사할 때 사용한다.
 * mem_brk - 1인 이유: mem_brk는 다음에 쓸 수 있는 첫 번째 주소이므로
 * 현재 유효한 마지막 주소는 mem_brk - 1이다.
 */
void *mem_heap_hi()
{
    return (void *)(mem_brk - 1);
}

/*
 * mem_heapsize - 현재 힙 사용 크기(바이트)를 반환한다.
 *
 * [계산]
 * mem_brk - mem_start_brk = 현재까지 확장된 힙의 총 크기
 */
size_t mem_heapsize()
{
    return (size_t)(mem_brk - mem_start_brk);
}

/*
 * mem_pagesize - 시스템의 페이지 크기(바이트)를 반환한다.
 *
 * [용도]
 * 일반적으로 4096바이트(4KB)이며, 힙 확장 단위를 페이지 크기에
 * 맞출 때 참고할 수 있다.
 */
size_t mem_pagesize()
{
    return (size_t)getpagesize();
}

