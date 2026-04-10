/*
 * mm-naive.c - 가장 단순한 (하지만 메모리 효율이 낮은) malloc 구현체.
 *
 * [동작 방식 - 개요]
 * 이 구현은 "묵시적 가용 리스트(implicit free list)" 방식을 사용하지 않는
 * 가장 단순한 접근법이다.
 *
 * - mm_malloc: brk 포인터를 단순히 증가시켜 블록을 할당한다.
 *   각 블록은 헤더(size_t 크기)와 페이로드(실제 데이터)로만 구성된다.
 *   푸터(footer)나 가용 리스트 관리가 없다.
 *
 * - mm_free: 아무 작업도 하지 않는다. (해제된 블록을 재사용하지 않음)
 *   이 때문에 메모리 효율이 매우 낮다.
 *
 * - mm_realloc: mm_malloc과 mm_free를 조합해 구현한다.
 *   새 블록을 할당하고, 기존 데이터를 복사한 뒤 이전 블록을 해제한다.
 *
 * [힙 블록 구조]
 * | size_t 헤더 (페이로드 크기 저장) | 페이로드 (실제 데이터) |
 *
 * [개선 방향]
 * 실제 성능을 높이려면 이 파일의 내용을 묵시적/명시적 가용 리스트,
 * 경계 태그(boundary tag), 분리 가용 리스트 등으로 교체해야 한다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*
 * 팀 정보를 아래 구조체에 입력해야 mdriver가 정상 동작한다.
 * teamname, name1, id1은 필수 항목이다.
 */
team_t team = {
    /* 팀 이름 */
    "ateam",
    /* 첫 번째 멤버 이름 */
    "Harry Bovik",
    /* 첫 번째 멤버 이메일 */
    "bovik@cs.cmu.edu",
    /* 두 번째 멤버 이름 (없으면 빈 문자열) */
    "",
    /* 두 번째 멤버 이메일 (없으면 빈 문자열) */
    ""};

/* 정렬 기준: 8바이트 더블 워드 정렬 */
#define ALIGNMENT 8

/*
 * ALIGN(size): size를 ALIGNMENT의 배수로 올림 정렬한다.
 * 예) size=9 → (9 + 7) & ~7 = 16
 * ~0x7 = ...11111000 → 하위 3비트를 0으로 만들어 8의 배수로 맞춤
 */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

/*
 * SIZE_T_SIZE: size_t 타입을 8바이트 정렬한 크기.
 * 각 블록의 헤더(크기 정보 저장용)로 사용된다.
 */
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*
 * mm_init - malloc 패키지를 초기화한다.
 *
 * [동작 방식]
 * 이 단순 구현에서는 별도 초기화 작업이 없다.
 * 실제 구현에서는 힙 프롤로그/에필로그 블록 설정 등이 필요하다.
 *
 * 반환값: 성공 시 0, 실패 시 -1
 */
int mm_init(void)
{
    return 0;
}

/*
 * mm_malloc - brk 포인터를 증가시켜 새 블록을 할당한다.
 *
 * [동작 방식]
 * 1. 요청 크기(size) + 헤더 크기(SIZE_T_SIZE)를 8바이트 정렬한 newsize 계산
 * 2. mem_sbrk(newsize)로 힙을 newsize만큼 확장
 * 3. 블록의 시작 위치(p)에 원래 요청 크기(size)를 헤더로 저장
 * 4. 헤더 바로 다음 주소(페이로드 시작 위치)를 반환
 *
 * [블록 레이아웃]
 * p → | size (SIZE_T_SIZE) | payload (size bytes) |
 *        ↑ 헤더              ↑ 반환하는 주소
 *
 * 반환값: 할당된 페이로드 시작 주소, 실패 시 NULL
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE); /* 헤더 포함 전체 블록 크기 (정렬됨) */
    void *p = mem_sbrk(newsize);             /* 힙을 newsize만큼 확장하고 이전 brk 반환 */
    if (p == (void *)-1)
        return NULL; /* 힙 확장 실패 */
    else
    {
        *(size_t *)p = size;                      /* 헤더에 페이로드 크기 저장 */
        return (void *)((char *)p + SIZE_T_SIZE); /* 페이로드 시작 주소 반환 */
    }
}

/*
 * mm_free - 블록을 해제한다.
 *
 * [동작 방식]
 * 이 단순 구현에서는 아무 작업도 하지 않는다.
 * 해제된 블록이 재사용되지 않으므로 메모리 효율이 매우 낮다.
 *
 * 실제 구현에서는 블록을 가용 리스트에 추가하고,
 * 인접한 가용 블록과 연결(coalescing)해야 한다.
 */
void mm_free(void *ptr)
{
}

/*
 * mm_realloc - 기존 블록의 크기를 변경한다.
 *
 * [동작 방식]
 * 1. mm_malloc(size)로 새 블록 할당
 * 2. 기존 블록(ptr)의 헤더에서 이전 크기 읽기
 * 3. min(size, oldsize)만큼 데이터를 새 블록으로 복사
 * 4. 기존 블록 해제 (이 구현에선 실제로 해제 안 됨)
 * 5. 새 블록 주소 반환
 *
 * [주의]
 * - ptr이 NULL이면 mm_malloc(size)와 동일하게 동작해야 하지만
 *   이 구현에서는 처리하지 않는다.
 * - size가 0이면 mm_free(ptr)와 동일하게 동작해야 하지만
 *   이 구현에서는 처리하지 않는다.
 *
 * 반환값: 새로 할당된 블록의 페이로드 시작 주소, 실패 시 NULL
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size); /* 새 크기의 블록 할당 */
    if (newptr == NULL)
        return NULL;

    /* 기존 블록의 헤더(페이로드 시작 - SIZE_T_SIZE 위치)에서 이전 크기 읽기 */
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);

    /* 새 크기가 더 작으면 새 크기만큼만 복사 (데이터 잘림 방지) */
    if (size < copySize)
        copySize = size;

    memcpy(newptr, oldptr, copySize); /* 이전 데이터 복사 */
    mm_free(oldptr);                  /* 이전 블록 해제 (이 구현에선 no-op) */
    return newptr;
}