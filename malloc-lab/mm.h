#include <stdio.h>

/* mm.c에서 구현하는 malloc 패키지 함수 선언 */
extern int mm_init (void);        /* 힙 초기화 */
extern void *mm_malloc (size_t size); /* 메모리 할당 */
extern void mm_free (void *ptr);      /* 메모리 해제 */
extern void *mm_realloc(void *ptr, size_t size); /* 메모리 재할당 */


/*
 * team_t - 팀 정보를 담는 구조체.
 * mm.c 내부에서 이 구조체의 인스턴스(team)를 정의해야 한다.
 * mdriver가 실행 시 이 정보를 출력하고 유효성을 검사한다.
 */
typedef struct {
    char *teamname; /* 팀 이름 (ID1+ID2 또는 ID1 단독) */
    char *name1;    /* 첫 번째 멤버 전체 이름 */
    char *id1;      /* 첫 번째 멤버 이메일 */
    char *name2;    /* 두 번째 멤버 전체 이름 (없으면 빈 문자열) */
    char *id2;      /* 두 번째 멤버 이메일 (없으면 빈 문자열) */
} team_t;

extern team_t team;

