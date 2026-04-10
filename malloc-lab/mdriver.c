/*
 * mdriver.c - CS:APP Malloc Lab 드라이버
 *
 * [역할]
 * 트레이스 파일들을 이용해 mm.c에 구현된 malloc/free/realloc을 테스트한다.
 *
 * [전체 동작 흐름]
 * 1. 커맨드라인 인자 파싱 (-f, -t, -l, -v, -V, -g, -a, -h)
 * 2. 팀 정보 출력 및 유효성 검사
 * 3. (옵션) libc malloc 성능 측정
 * 4. mm.c의 malloc 패키지 정확성·공간 효율성·처리량 측정
 * 5. 성능 지수(Perf index) 계산 및 출력
 *    - 성능 지수 = 공간 이용률(util) * 60% + 처리량(throughput) * 40%
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * 허가 없이 사용, 수정, 복사 금지.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <time.h>

extern char *optarg; /* getopt()에서 옵션 인자 문자열을 가리키는 포인터 선언 */

#include "mm.h"
#include "memlib.h"
#include "fsecs.h"
#include "config.h"

/**********************
 * 상수 및 매크로 정의
 **********************/

/* 기타 */
#define MAXLINE 1024	   /* 문자열 최대 길이 */
#define HDRLINES 4		   /* 트레이스 파일 헤더 줄 수 */
#define LINENUM(i) (i + 5) /* 트레이스 요청 번호를 실제 파일 줄 번호로 변환 (1-based) */

/* IS_ALIGNED(p): p가 ALIGNMENT 바이트로 정렬되어 있으면 참 */
#define IS_ALIGNED(p) ((((unsigned int)(p)) % ALIGNMENT) == 0)

/******************************
 * 핵심 복합 자료형 정의
 *****************************/

/*
 * range_t - 할당된 블록 페이로드의 범위를 기록하는 연결 리스트 노드.
 * mdriver가 블록 간 겹침 여부를 검사할 때 사용한다.
 */
typedef struct range_t
{
	char *lo;			  /* 페이로드 시작 주소 */
	char *hi;			  /* 페이로드 끝 주소 */
	struct range_t *next; /* 다음 노드 (연결 리스트) */
} range_t;

/*
 * traceop_t - 트레이스 파일의 단일 할당 요청을 나타내는 구조체.
 * 각 요청은 할당(ALLOC), 해제(FREE), 재할당(REALLOC) 중 하나이다.
 */
typedef struct
{
	enum
	{
		ALLOC,   /* malloc 요청 */
		FREE,    /* free 요청 */
		REALLOC  /* realloc 요청 */
	} type;	   /* 요청 종류 */
	int index; /* 이후 free/realloc에서 참조할 블록 인덱스 */
	int size;  /* alloc/realloc 요청 크기 (바이트) */
} traceop_t;

/*
 * trace_t - 하나의 트레이스 파일 전체를 메모리에 저장하는 구조체.
 *
 * [트레이스 파일 형식]
 * 헤더: suggested_heap_size, num_ids, num_ops, weight
 * 본문: 각 줄이 'a index size' / 'r index size' / 'f index' 형태
 */
typedef struct
{
	int sugg_heapsize;	 /* 권장 힙 크기 (현재 미사용) */
	int num_ids;		 /* alloc/realloc에서 사용되는 고유 ID 수 */
	int num_ops;		 /* 전체 요청 수 */
	int weight;			 /* 트레이스 가중치 (현재 미사용) */
	traceop_t *ops;		 /* 요청 배열 (num_ops개) */
	char **blocks;		 /* 각 ID별로 할당된 블록 포인터 배열 */
	size_t *block_sizes; /* 각 ID별 페이로드 크기 배열 */
} trace_t;

/*
 * speed_t - fcyc()에 전달하는 파라미터 구조체.
 * fcyc()는 함수 포인터와 하나의 void* 인자만 받으므로,
 * 여러 값을 전달하기 위해 이 구조체를 사용한다.
 */
typedef struct
{
	trace_t *trace;
	range_t *ranges;
} speed_t;

/*
 * stats_t - 특정 트레이스에 대한 malloc 패키지의 성능 통계 요약.
 */
typedef struct
{
	/* libc malloc과 학생 malloc 패키지 공통 필드 */
	double ops;	 /* 트레이스의 총 연산 수 (malloc/free/realloc 합계) */
	int valid;	 /* 해당 트레이스를 올바르게 처리했으면 1, 아니면 0 */
	double secs; /* 트레이스 실행에 걸린 시간 (초) */

	/* 학생 malloc 패키지 전용 필드 */
	double util; /* 공간 이용률 (libc는 항상 0) */

	/* 주의: secs와 util은 valid == 1일 때만 유효한 값을 가짐 */
} stats_t;

/********************
 * 전역 변수
 *******************/
int verbose = 0;	   /* 상세 출력 모드 플래그 (-v 또는 -V 옵션으로 설정) */
static int errors = 0; /* 학생 malloc 실행 중 발견된 오류 수 */
char msg[MAXLINE];	   /* 오류 메시지 조합용 임시 버퍼 */

/* 기본 트레이스 파일들이 있는 디렉토리 경로 */
static char tracedir[MAXLINE] = TRACEDIR;

/* 기본 트레이스 파일 이름 목록 (config.h의 DEFAULT_TRACEFILES에서 가져옴) */
static char *default_tracefiles[] = {
	DEFAULT_TRACEFILES, NULL};

/*********************
 * 함수 프로토타입 선언
 *********************/

/* 범위 리스트(range list) 조작 함수들 */
static int add_range(range_t **ranges, char *lo, int size,
					 int tracenum, int opnum);
static void remove_range(range_t **ranges, char *lo);
static void clear_ranges(range_t **ranges);

/* 트레이스 파일 읽기/해제 함수들 */
static trace_t *read_trace(char *tracedir, char *filename);
static void free_trace(trace_t *trace);

/* libc malloc의 정확성·속도 평가 함수들 */
static int eval_libc_valid(trace_t *trace, int tracenum);
static void eval_libc_speed(void *ptr);

/* 학생 malloc 패키지(mm.c)의 정확성·공간 이용률·속도 평가 함수들 */
static int eval_mm_valid(trace_t *trace, int tracenum, range_t **ranges);
static double eval_mm_util(trace_t *trace, int tracenum, range_t **ranges);
static void eval_mm_speed(void *ptr);

/* 기타 헬퍼 함수들 */
static void printresults(int n, stats_t *stats);
static void usage(void);
static void unix_error(char *msg);
static void malloc_error(int tracenum, int opnum, char *msg);
static void app_error(char *msg);

/**************
 * main 함수
 **************/
int main(int argc, char **argv)
{
	int i;
	int c;
	char **tracefiles = NULL;	/* 트레이스 파일 이름 배열 (NULL 종료) */
	int num_tracefiles = 0;		/* 트레이스 파일 수 */
	trace_t *trace = NULL;		/* 메모리에 읽어온 트레이스 파일 */
	range_t *ranges = NULL;		/* 한 트레이스의 블록 범위 추적 리스트 */
	stats_t *libc_stats = NULL; /* libc malloc 통계 (트레이스별) */
	stats_t *mm_stats = NULL;	/* 학생 malloc 통계 (트레이스별) */
	speed_t speed_params;		/* eval_mm_speed 등에 전달할 파라미터 */

	int team_check = 1; /* 팀 구조체 유효성 검사 여부 (-a 옵션으로 끌 수 있음) */
	int run_libc = 0;	/* libc malloc도 함께 실행할지 여부 (-l 옵션) */
	int autograder = 0; /* 자동 채점기용 요약 정보 출력 여부 (-g 옵션) */

	/* 성능 지수 계산에 사용되는 임시 변수들 */
	double secs, ops, util, avg_mm_util, avg_mm_throughput, p1, p2, perfindex;
	int numcorrect;

	/*
	 * 커맨드라인 인자 파싱
	 * getopt()로 각 옵션을 읽어 해당 변수를 설정한다.
	 */
	while ((c = getopt(argc, argv, "f:t:hvVgal")) != EOF)
	{
		printf("getopt returned: %d\n", c); /* 디버깅용 출력 */

		switch (c)
		{
		case 'g': /* 자동 채점기용 요약 정보 생성 */
			autograder = 1;
			break;
		case 'f': /* 특정 트레이스 파일 하나만 사용 (현재 디렉토리 기준) */
			num_tracefiles = 1;
			if ((tracefiles = realloc(tracefiles, 2 * sizeof(char *))) == NULL)
				unix_error("ERROR: realloc failed in main");
			strcpy(tracedir, "./");
			tracefiles[0] = strdup(optarg);
			tracefiles[1] = NULL;
			break;
		case 't': /* 트레이스 파일들이 있는 디렉토리 지정 */
			if (num_tracefiles == 1) /* -f가 이미 지정된 경우 무시 */
				break;
			strcpy(tracedir, optarg);
			if (tracedir[strlen(tracedir) - 1] != '/')
				strcat(tracedir, "/"); /* 경로는 항상 '/'로 끝나야 함 */
			break;
		case 'a': /* 팀 구조체 검사 비활성화 */
			team_check = 0;
			break;
		case 'l': /* libc malloc도 함께 실행 */
			run_libc = 1;
			break;
		case 'v': /* 트레이스별 성능 출력 */
			verbose = 1;
			break;
		case 'V': /* 더 상세한 디버그 출력 */
			verbose = 2;
			break;
		case 'h': /* 도움말 출력 후 종료 */
			usage();
			exit(0);
		default:
			usage();
			exit(1);
		}
	}

	/*
	 * 팀 정보 검사 및 출력
	 * teamname, name1, id1이 비어있으면 오류 처리한다.
	 */
	if (team_check)
	{
		if (!strcmp(team.teamname, ""))
		{
			printf("ERROR: Please provide the information about your team in mm.c.\n");
			exit(1);
		}
		else
			printf("Team Name:%s\n", team.teamname);
		if ((*team.name1 == '\0') || (*team.id1 == '\0'))
		{
			printf("ERROR.  You must fill in all team member 1 fields!\n");
			exit(1);
		}
		else
			printf("Member 1 :%s:%s\n", team.name1, team.id1);

		if (((*team.name2 != '\0') && (*team.id2 == '\0')) ||
			((*team.name2 == '\0') && (*team.id2 != '\0')))
		{
			printf("ERROR.  You must fill in all or none of the team member 2 ID fields!\n");
			exit(1);
		}
		else if (*team.name2 != '\0')
			printf("Member 2 :%s:%s\n", team.name2, team.id2);
	}

	/*
	 * -f 옵션이 없으면 config.h의 DEFAULT_TRACEFILES를 사용한다.
	 */
	if (tracefiles == NULL)
	{
		tracefiles = default_tracefiles;
		num_tracefiles = sizeof(default_tracefiles) / sizeof(char *) - 1;
		printf("Using default tracefiles in %s\n", tracedir);
	}

	/* 시간 측정 패키지 초기화 (fsecs.c/fcyc.c 참조) */
	init_fsecs();

	/*
	 * (옵션) libc malloc 실행 및 평가
	 * -l 옵션이 있을 때만 수행한다.
	 */
	if (run_libc)
	{
		if (verbose > 1)
			printf("\nTesting libc malloc\n");

		/* 트레이스 파일 수만큼 stats_t 배열 할당 */
		libc_stats = (stats_t *)calloc(num_tracefiles, sizeof(stats_t));
		if (libc_stats == NULL)
			unix_error("libc_stats calloc in main failed");

		/* 각 트레이스에 대해 libc malloc 정확성 검사 후 속도 측정 */
		for (i = 0; i < num_tracefiles; i++)
		{
			trace = read_trace(tracedir, tracefiles[i]);
			libc_stats[i].ops = trace->num_ops;
			if (verbose > 1)
				printf("Checking libc malloc for correctness, ");
			libc_stats[i].valid = eval_libc_valid(trace, i);
			if (libc_stats[i].valid)
			{
				speed_params.trace = trace;
				if (verbose > 1)
					printf("and performance.\n");
				/* fsecs()가 eval_libc_speed를 여러 번 실행해 평균 시간 측정 */
				libc_stats[i].secs = fsecs(eval_libc_speed, &speed_params);
			}
			free_trace(trace);
		}

		/* libc 결과 표 출력 */
		if (verbose)
		{
			printf("\nResults for libc malloc:\n");
			printresults(num_tracefiles, libc_stats);
		}
	}

	/*
	 * 학생 mm malloc 패키지 항상 실행 및 평가
	 */
	if (verbose > 1)
		printf("\nTesting mm malloc\n");

	/* 트레이스 파일 수만큼 stats_t 배열 할당 */
	mm_stats = (stats_t *)calloc(num_tracefiles, sizeof(stats_t));
	if (mm_stats == NULL)
		unix_error("mm_stats calloc in main failed");

	/* memlib.c의 가상 메모리 시스템 초기화 */
	mem_init();

	/* 각 트레이스에 대해 mm malloc 패키지 평가 */
	for (i = 0; i < num_tracefiles; i++)
	{
		trace = read_trace(tracedir, tracefiles[i]);
		mm_stats[i].ops = trace->num_ops;
		if (verbose > 1)
			printf("Checking mm_malloc for correctness, ");
		/* 1단계: 정확성 검사 (블록 겹침, 정렬, 힙 범위 등) */
		mm_stats[i].valid = eval_mm_valid(trace, i, &ranges);
		if (mm_stats[i].valid)
		{
			if (verbose > 1)
				printf("efficiency, ");
			/* 2단계: 공간 이용률 측정 (최대 사용량 / 힙 크기) */
			mm_stats[i].util = eval_mm_util(trace, i, &ranges);
			speed_params.trace = trace;
			speed_params.ranges = ranges;
			if (verbose > 1)
				printf("and performance.\n");
			/* 3단계: 처리량 측정 (초당 연산 수) */
			mm_stats[i].secs = fsecs(eval_mm_speed, &speed_params);
		}
		free_trace(trace);
	}

	/* mm 결과 표 출력 */
	if (verbose)
	{
		printf("\nResults for mm malloc:\n");
		printresults(num_tracefiles, mm_stats);
		printf("\n");
	}

	/*
	 * 학생 mm 패키지의 종합 통계 누산
	 */
	secs = 0;
	ops = 0;
	util = 0;
	numcorrect = 0;
	for (i = 0; i < num_tracefiles; i++)
	{
		secs += mm_stats[i].secs;
		ops += mm_stats[i].ops;
		util += mm_stats[i].util;
		if (mm_stats[i].valid)
			numcorrect++;
	}
	avg_mm_util = util / num_tracefiles; /* 평균 공간 이용률 */

	/*
	 * 성능 지수(Perf index) 계산 및 출력
	 *
	 * [계산 공식]
	 * p1 = UTIL_WEIGHT(0.6) * avg_mm_util
	 * p2 = (1 - UTIL_WEIGHT)(0.4) * min(처리량 / AVG_LIBC_THRUPUT, 1.0)
	 * perfindex = (p1 + p2) * 100
	 *
	 * 처리량이 libc 기준(AVG_LIBC_THRUPUT)을 초과하면 처리량 점수는 만점
	 */
	if (errors == 0)
	{
		avg_mm_throughput = ops / secs; /* 초당 연산 수 */

		p1 = UTIL_WEIGHT * avg_mm_util; /* 공간 이용률 점수 */
		if (avg_mm_throughput > AVG_LIBC_THRUPUT)
		{
			p2 = (double)(1.0 - UTIL_WEIGHT); /* 처리량이 libc 초과 → 만점 */
		}
		else
		{
			p2 = ((double)(1.0 - UTIL_WEIGHT)) *
				 (avg_mm_throughput / AVG_LIBC_THRUPUT); /* 처리량 비율에 따라 감점 */
		}

		perfindex = (p1 + p2) * 100.0;
		printf("Perf index = %.0f (util) + %.0f (thru) = %.0f/100\n",
			   p1 * 100,
			   p2 * 100,
			   perfindex);
	}
	else
	{ /* 오류가 있으면 성능 지수 0 */
		perfindex = 0.0;
		printf("Terminated with %d errors\n", errors);
	}

	/* 자동 채점기용 출력 */
	if (autograder)
	{
		printf("correct:%d\n", numcorrect);
		printf("perfidx:%.0f\n", perfindex);
	}

	exit(0);
}

/*****************************************************************
 * 범위 리스트(range list) 조작 함수들
 *
 * 범위 리스트는 현재 할당된 블록들의 페이로드 범위를 추적한다.
 * 새 블록이 기존 블록과 겹치는지 검사하는 데 사용된다.
 ****************************************************************/

/*
 * add_range - 새로 할당된 블록을 범위 리스트에 추가한다.
 *
 * [동작 방식]
 * 1. 주소 정렬 검사: lo가 ALIGNMENT 바이트로 정렬되어 있는지 확인
 * 2. 힙 범위 검사: [lo, hi] 전체가 현재 힙 내에 있는지 확인
 * 3. 겹침 검사: 기존 블록들과 범위가 겹치지 않는지 확인
 * 4. 검사 통과 시 새 range_t 노드를 리스트 앞에 추가
 *
 * 반환값: 성공 시 1, 오류 시 0
 */
static int add_range(range_t **ranges, char *lo, int size,
					 int tracenum, int opnum)
{
	char *hi = lo + size - 1; /* 페이로드 끝 주소 */
	range_t *p;
	char msg[MAXLINE];

	assert(size > 0);

	/* 페이로드 주소가 ALIGNMENT 바이트로 정렬되어 있어야 함 */
	if (!IS_ALIGNED(lo))
	{
		sprintf(msg, "Payload address (%p) not aligned to %d bytes",
				lo, ALIGNMENT);
		malloc_error(tracenum, opnum, msg);
		return 0;
	}

	/* 페이로드 전체 범위가 현재 힙 안에 있어야 함 */
	if ((lo < (char *)mem_heap_lo()) || (lo > (char *)mem_heap_hi()) ||
		(hi < (char *)mem_heap_lo()) || (hi > (char *)mem_heap_hi()))
	{
		sprintf(msg, "Payload (%p:%p) lies outside heap (%p:%p)",
				lo, hi, mem_heap_lo(), mem_heap_hi());
		malloc_error(tracenum, opnum, msg);
		return 0;
	}

	/* 기존 블록들과 범위가 겹치면 안 됨 */
	for (p = *ranges; p != NULL; p = p->next)
	{
		if ((lo >= p->lo && lo <= p->hi) ||
			(hi >= p->lo && hi <= p->hi))
		{
			sprintf(msg, "Payload (%p:%p) overlaps another payload (%p:%p)\n",
					lo, hi, p->lo, p->hi);
			malloc_error(tracenum, opnum, msg);
			return 0;
		}
	}

	/*
	 * 모든 검사 통과: 새 range_t 노드를 생성하여 리스트 맨 앞에 추가
	 */
	if ((p = (range_t *)malloc(sizeof(range_t))) == NULL)
		unix_error("malloc error in add_range");
	p->next = *ranges;
	p->lo = lo;
	p->hi = hi;
	*ranges = p;
	return 1;
}

/*
 * remove_range - 페이로드 시작 주소가 lo인 블록의 범위 레코드를 삭제한다.
 *
 * [동작 방식]
 * 연결 리스트를 순회하여 lo와 일치하는 노드를 찾아 제거하고 free()한다.
 */
static void remove_range(range_t **ranges, char *lo)
{
	range_t *p;
	range_t **prevpp = ranges; /* 이전 노드의 next 포인터를 가리키는 이중 포인터 */
	int size;

	for (p = *ranges; p != NULL; p = p->next)
	{
		if (p->lo == lo)
		{
			*prevpp = p->next; /* 이전 노드의 next를 현재 노드의 next로 연결 */
			size = p->hi - p->lo + 1;
			free(p);
			break;
		}
		prevpp = &(p->next);
	}
}

/*
 * clear_ranges - 트레이스의 범위 레코드를 모두 해제한다.
 *
 * [동작 방식]
 * 리스트의 모든 노드를 순회하면서 free()하고, 리스트 포인터를 NULL로 초기화한다.
 */
static void clear_ranges(range_t **ranges)
{
	range_t *p;
	range_t *pnext;

	for (p = *ranges; p != NULL; p = pnext)
	{
		pnext = p->next;
		free(p);
	}
	*ranges = NULL;
}

/**********************************************
 * 트레이스 파일 조작 함수들
 *********************************************/

/*
 * read_trace - 트레이스 파일을 읽어서 메모리에 저장한다.
 *
 * [트레이스 파일 형식]
 * 헤더 4줄: sugg_heapsize, num_ids, num_ops, weight
 * 본문: 각 줄이 'a index size' (alloc) / 'r index size' (realloc) / 'f index' (free)
 *
 * [동작 방식]
 * 1. trace_t 구조체 할당
 * 2. 파일 열기 및 헤더 파싱
 * 3. ops 배열, blocks 배열, block_sizes 배열 동적 할당
 * 4. 본문의 각 요청 줄을 파싱하여 ops 배열에 저장
 *
 * 반환값: 읽어온 trace_t 포인터
 */
static trace_t *read_trace(char *tracedir, char *filename)
{
	FILE *tracefile;
	trace_t *trace;
	char type[MAXLINE];
	char path[MAXLINE];
	unsigned index, size;
	unsigned max_index = 0;
	unsigned op_index;

	if (verbose > 1)
		printf("Reading tracefile: %s\n", filename);

	/* trace_t 구조체 할당 */
	if ((trace = (trace_t *)malloc(sizeof(trace_t))) == NULL)
		unix_error("malloc 1 failed in read_trance");

	/* 트레이스 파일 경로 조합 후 열기 */
	strcpy(path, tracedir);
	strcat(path, filename);
	if ((tracefile = fopen(path, "r")) == NULL)
	{
		sprintf(msg, "Could not open %s in read_trace", path);
		unix_error(msg);
	}

	/* 헤더 파싱 */
	fscanf(tracefile, "%d", &(trace->sugg_heapsize)); /* 권장 힙 크기 (미사용) */
	fscanf(tracefile, "%d", &(trace->num_ids));        /* 고유 ID 수 */
	fscanf(tracefile, "%d", &(trace->num_ops));        /* 총 연산 수 */
	fscanf(tracefile, "%d", &(trace->weight));         /* 가중치 (미사용) */

	/* 요청 배열 할당 */
	if ((trace->ops =
			 (traceop_t *)malloc(trace->num_ops * sizeof(traceop_t))) == NULL)
		unix_error("malloc 2 failed in read_trace");

	/* 블록 포인터 배열 할당 (각 ID별 할당된 주소 저장용) */
	if ((trace->blocks =
			 (char **)malloc(trace->num_ids * sizeof(char *))) == NULL)
		unix_error("malloc 3 failed in read_trace");

	/* 각 ID별 페이로드 크기 저장 배열 할당 */
	if ((trace->block_sizes =
			 (size_t *)malloc(trace->num_ids * sizeof(size_t))) == NULL)
		unix_error("malloc 4 failed in read_trace");

	/* 본문의 각 요청 줄 파싱 */
	index = 0;
	op_index = 0;
	while (fscanf(tracefile, "%s", type) != EOF)
	{
		switch (type[0])
		{
		case 'a': /* alloc 요청: 'a index size' */
			fscanf(tracefile, "%u %u", &index, &size);
			trace->ops[op_index].type = ALLOC;
			trace->ops[op_index].index = index;
			trace->ops[op_index].size = size;
			max_index = (index > max_index) ? index : max_index;
			break;
		case 'r': /* realloc 요청: 'r index size' */
			fscanf(tracefile, "%u %u", &index, &size);
			trace->ops[op_index].type = REALLOC;
			trace->ops[op_index].index = index;
			trace->ops[op_index].size = size;
			max_index = (index > max_index) ? index : max_index;
			break;
		case 'f': /* free 요청: 'f index' */
			fscanf(tracefile, "%ud", &index);
			trace->ops[op_index].type = FREE;
			trace->ops[op_index].index = index;
			break;
		default:
			printf("Bogus type character (%c) in tracefile %s\n",
				   type[0], path);
			exit(1);
		}
		op_index++;
	}
	fclose(tracefile);
	assert(max_index == trace->num_ids - 1);
	assert(trace->num_ops == op_index);

	return trace;
}

/*
 * free_trace - read_trace()에서 할당한 trace_t와 세 배열을 모두 해제한다.
 */
void free_trace(trace_t *trace)
{
	free(trace->ops);         /* 요청 배열 해제 */
	free(trace->blocks);      /* 블록 포인터 배열 해제 */
	free(trace->block_sizes); /* 블록 크기 배열 해제 */
	free(trace);              /* trace_t 구조체 자체 해제 */
}

/**********************************************************************
 * 할당기 정확성·공간 이용률·처리량 평가 함수들
 *
 * libc malloc과 학생 mm malloc 패키지 모두에 대해 평가를 수행한다.
 **********************************************************************/

/*
 * eval_mm_valid - mm malloc 패키지의 정확성을 검사한다.
 *
 * [검사 항목]
 * 1. mm_init 성공 여부
 * 2. mm_malloc: NULL 반환 여부, 주소 정렬, 힙 범위, 기존 블록과 겹침
 * 3. mm_realloc: 이전 데이터 보존 여부, 새 블록 정확성
 * 4. mm_free: 정상 해제 여부
 *
 * [데이터 검증 방법]
 * alloc 시 블록을 (index & 0xFF) 패턴으로 채우고,
 * realloc 시 해당 패턴이 그대로 유지되는지 확인한다.
 *
 * 반환값: 정확하면 1, 오류 발생 시 0
 */
static int eval_mm_valid(trace_t *trace, int tracenum, range_t **ranges)
{
	int i, j;
	int index;
	int size;
	int oldsize;
	char *newp;
	char *oldp;
	char *p;

	/* 힙 초기화 및 범위 리스트 초기화 */
	mem_reset_brk();
	clear_ranges(ranges);

	/* mm_init 호출 */
	if (mm_init() < 0)
	{
		malloc_error(tracenum, 0, "mm_init failed.");
		return 0;
	}

	/* 트레이스의 각 요청을 순서대로 처리 */
	for (i = 0; i < trace->num_ops; i++)
	{
		index = trace->ops[i].index;
		size = trace->ops[i].size;

		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_malloc 테스트 */

			/* 학생 malloc 호출 */
			if ((p = mm_malloc(size)) == NULL)
			{
				malloc_error(tracenum, i, "mm_malloc failed.");
				return 0;
			}

			/*
			 * 새 블록의 범위를 검사하고 범위 리스트에 추가한다.
			 * (정렬, 힙 범위, 겹침 여부 모두 검사)
			 */
			if (add_range(ranges, p, size, tracenum, i) == 0)
				return 0;

			/*
			 * 블록을 (index & 0xFF) 패턴으로 채운다.
			 * 이후 realloc 시 데이터 복사가 올바르게 됐는지 확인하기 위함이다.
			 */
			memset(p, index & 0xFF, size);

			/* 블록 포인터와 크기 기록 */
			trace->blocks[index] = p;
			trace->block_sizes[index] = size;
			break;

		case REALLOC: /* mm_realloc 테스트 */

			/* 학생 realloc 호출 */
			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, size)) == NULL)
			{
				malloc_error(tracenum, i, "mm_realloc failed.");
				return 0;
			}

			/* 이전 범위 제거 */
			remove_range(ranges, oldp);

			/* 새 블록 범위 검사 및 추가 */
			if (add_range(ranges, newp, size, tracenum, i) == 0)
				return 0;

			/*
			 * 이전 데이터가 새 블록에 올바르게 복사됐는지 검사한다.
			 * min(size, oldsize) 범위까지 (index & 0xFF) 패턴인지 확인.
			 */
			oldsize = trace->block_sizes[index];
			if (size < oldsize)
				oldsize = size;
			for (j = 0; j < oldsize; j++)
			{
				if (newp[j] != (index & 0xFF))
				{
					malloc_error(tracenum, i, "mm_realloc did not preserve the "
											  "data from old block");
					return 0;
				}
			}
			/* 새 블록을 새 패턴으로 다시 채움 */
			memset(newp, index & 0xFF, size);

			/* 블록 포인터와 크기 갱신 */
			trace->blocks[index] = newp;
			trace->block_sizes[index] = size;
			break;

		case FREE: /* mm_free 테스트 */

			/* 범위 리스트에서 제거 후 학생 free 호출 */
			p = trace->blocks[index];
			remove_range(ranges, p);
			mm_free(p);
			break;

		default:
			app_error("Nonexistent request type in eval_mm_valid");
		}
	}

	/* 모든 검사 통과: 정확한 malloc 패키지 */
	return 1;
}

/*
 * eval_mm_util - 학생 malloc 패키지의 공간 이용률을 측정한다.
 *
 * [측정 방식]
 * - "최적 할당기"가 내부 단편화와 외부 단편화 없이 할당했을 때
 *   필요한 최대 힙 크기(hwm: high water mark)를 추적한다.
 * - 공간 이용률 = hwm / 실제 힙 크기
 *
 * [계산 방법]
 * total_size: 현재 순간에 할당된 모든 블록의 페이로드 크기 합
 * max_total_size: total_size의 최댓값 (= hwm)
 * mem_heapsize(): 학생 malloc이 실제로 사용한 힙 크기
 *
 * 이용률이 1.0에 가까울수록 내부/외부 단편화가 적다는 의미이다.
 *
 * 반환값: 공간 이용률 (0.0 ~ 1.0)
 */
static double eval_mm_util(trace_t *trace, int tracenum, range_t **ranges)
{
	int i;
	int index;
	int size, newsize, oldsize;
	int max_total_size = 0;
	int total_size = 0;
	char *p;
	char *newp, *oldp;

	/* 힙 초기화 및 mm 패키지 재초기화 */
	mem_reset_brk();
	if (mm_init() < 0)
		app_error("mm_init failed in eval_mm_util");

	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{

		case ALLOC: /* 할당: total_size 증가 */
			index = trace->ops[i].index;
			size = trace->ops[i].size;

			if ((p = mm_malloc(size)) == NULL)
				app_error("mm_malloc failed in eval_mm_util");

			/* 블록 포인터와 크기 저장 */
			trace->blocks[index] = p;
			trace->block_sizes[index] = size;

			/* 현재 할당된 총 페이로드 크기 갱신 */
			total_size += size;

			/* 최댓값 갱신 */
			max_total_size = (total_size > max_total_size) ? total_size : max_total_size;
			break;

		case REALLOC: /* 재할당: total_size 변화량 반영 */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldsize = trace->block_sizes[index];

			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, newsize)) == NULL)
				app_error("mm_realloc failed in eval_mm_util");

			/* 블록 포인터와 크기 갱신 */
			trace->blocks[index] = newp;
			trace->block_sizes[index] = newsize;

			/* 크기 변화량만큼 total_size 조정 */
			total_size += (newsize - oldsize);

			/* 최댓값 갱신 */
			max_total_size = (total_size > max_total_size) ? total_size : max_total_size;
			break;

		case FREE: /* 해제: total_size 감소 */
			index = trace->ops[i].index;
			size = trace->block_sizes[index];
			p = trace->blocks[index];

			mm_free(p);

			/* 해제된 크기만큼 total_size 감소 */
			total_size -= size;

			break;

		default:
			app_error("Nonexistent request type in eval_mm_util");
		}
	}

	/* 공간 이용률 = 최대 페이로드 합 / 실제 힙 크기 */
	return ((double)max_total_size / (double)mem_heapsize());
}

/*
 * eval_mm_speed - fcyc()가 처리량 측정 시 반복 호출하는 함수.
 *
 * [동작 방식]
 * eval_mm_valid()와 동일하게 트레이스를 재생하지만,
 * 정확성 검사는 하지 않고 속도만 측정한다.
 * fcyc()가 이 함수를 여러 번 호출하여 평균 실행 시간을 계산한다.
 */
static void eval_mm_speed(void *ptr)
{
	int i, index, size, newsize;
	char *p, *newp, *oldp, *block;
	trace_t *trace = ((speed_t *)ptr)->trace;

	/* 힙 초기화 및 mm 패키지 재초기화 */
	mem_reset_brk();
	if (mm_init() < 0)
		app_error("mm_init failed in eval_mm_speed");

	/* 트레이스의 각 요청을 순서대로 처리 (속도 측정용) */
	for (i = 0; i < trace->num_ops; i++)
		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_malloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;
			if ((p = mm_malloc(size)) == NULL)
				app_error("mm_malloc error in eval_mm_speed");
			trace->blocks[index] = p;
			break;

		case REALLOC: /* mm_realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, newsize)) == NULL)
				app_error("mm_realloc error in eval_mm_speed");
			trace->blocks[index] = newp;
			break;

		case FREE: /* mm_free */
			index = trace->ops[i].index;
			block = trace->blocks[index];
			mm_free(block);
			break;

		default:
			app_error("Nonexistent request type in eval_mm_valid");
		}
}

/*
 * eval_libc_valid - libc malloc이 트레이스를 오류 없이 완주하는지 확인한다.
 *
 * [동작 방식]
 * libc malloc/realloc/free를 호출하고, NULL 반환 시 오류 처리한다.
 * 학생 구현과 달리 정렬이나 겹침 검사는 하지 않는다.
 *
 * 반환값: 성공 시 1
 */
static int eval_libc_valid(trace_t *trace, int tracenum)
{
	int i, newsize;
	char *p, *newp, *oldp;

	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{

		case ALLOC: /* malloc */
			if ((p = malloc(trace->ops[i].size)) == NULL)
			{
				malloc_error(tracenum, i, "libc malloc failed");
				unix_error("System message");
			}
			trace->blocks[trace->ops[i].index] = p;
			break;

		case REALLOC: /* realloc */
			newsize = trace->ops[i].size;
			oldp = trace->blocks[trace->ops[i].index];
			if ((newp = realloc(oldp, newsize)) == NULL)
			{
				malloc_error(tracenum, i, "libc realloc failed");
				unix_error("System message");
			}
			trace->blocks[trace->ops[i].index] = newp;
			break;

		case FREE: /* free */
			free(trace->blocks[trace->ops[i].index]);
			break;

		default:
			app_error("invalid operation type  in eval_libc_valid");
		}
	}

	return 1;
}

/*
 * eval_libc_speed - fcyc()가 libc malloc 처리량 측정 시 반복 호출하는 함수.
 *
 * [동작 방식]
 * libc malloc/realloc/free로 트레이스를 재생한다.
 * 속도 측정 전용이므로 정확성 검사는 생략한다.
 */
static void eval_libc_speed(void *ptr)
{
	int i;
	int index, size, newsize;
	char *p, *newp, *oldp, *block;
	trace_t *trace = ((speed_t *)ptr)->trace;

	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{
		case ALLOC: /* malloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;
			if ((p = malloc(size)) == NULL)
				unix_error("malloc failed in eval_libc_speed");
			trace->blocks[index] = p;
			break;

		case REALLOC: /* realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldp = trace->blocks[index];
			if ((newp = realloc(oldp, newsize)) == NULL)
				unix_error("realloc failed in eval_libc_speed\n");

			trace->blocks[index] = newp;
			break;

		case FREE: /* free */
			index = trace->ops[i].index;
			block = trace->blocks[index];
			free(block);
			break;
		}
	}
}

/*************************************
 * 기타 헬퍼 함수들
 ************************************/

/*
 * printresults - malloc 패키지의 성능 요약 표를 출력한다.
 *
 * [출력 형식]
 * trace  valid  util    ops      secs   Kops
 * -----  -----  ----  ------  --------  ----
 *   0      yes   99%    5000  0.000100  1000
 *  ...
 * Total         99%   50000  0.001000  1000
 */
static void printresults(int n, stats_t *stats)
{
	int i;
	double secs = 0;
	double ops = 0;
	double util = 0;

	/* 각 트레이스별 결과 출력 */
	printf("%5s%7s %5s%8s%10s%6s\n",
		   "trace", " valid", "util", "ops", "secs", "Kops");
	for (i = 0; i < n; i++)
	{
		if (stats[i].valid)
		{
			printf("%2d%10s%5.0f%%%8.0f%10.6f%6.0f\n",
				   i,
				   "yes",
				   stats[i].util * 100.0,
				   stats[i].ops,
				   stats[i].secs,
				   (stats[i].ops / 1e3) / stats[i].secs);
			secs += stats[i].secs;
			ops += stats[i].ops;
			util += stats[i].util;
		}
		else
		{
			printf("%2d%10s%6s%8s%10s%6s\n",
				   i,
				   "no",
				   "-",
				   "-",
				   "-",
				   "-");
		}
	}

	/* 전체 합계 출력 */
	if (errors == 0)
	{
		printf("%12s%5.0f%%%8.0f%10.6f%6.0f\n",
			   "Total       ",
			   (util / n) * 100.0,
			   ops,
			   secs,
			   (ops / 1e3) / secs);
	}
	else
	{
		printf("%12s%6s%8s%10s%6s\n",
			   "Total       ",
			   "-",
			   "-",
			   "-",
			   "-");
	}
}

/*
 * app_error - 애플리케이션 오류 메시지를 출력하고 프로그램을 종료한다.
 */
void app_error(char *msg)
{
	printf("%s\n", msg);
	exit(1);
}

/*
 * unix_error - Unix 스타일 오류(errno 포함) 메시지를 출력하고 종료한다.
 */
void unix_error(char *msg)
{
	printf("%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * malloc_error - mm_malloc 패키지에서 발생한 오류를 보고한다.
 * 오류 카운터(errors)를 증가시키고 트레이스/줄 번호와 함께 메시지를 출력한다.
 */
void malloc_error(int tracenum, int opnum, char *msg)
{
	errors++;
	printf("ERROR [trace %d, line %d]: %s\n", tracenum, LINENUM(opnum), msg);
}

/*
 * usage - 커맨드라인 사용법을 출력한다.
 */
static void usage(void)
{
	fprintf(stderr, "Usage: mdriver [-hvVal] [-f <file>] [-t <dir>]\n");
	fprintf(stderr, "Options\n");
	fprintf(stderr, "\t-a         Don't check the team structure.\n");
	fprintf(stderr, "\t-f <file>  Use <file> as the trace file.\n");
	fprintf(stderr, "\t-g         Generate summary info for autograder.\n");
	fprintf(stderr, "\t-h         Print this message.\n");
	fprintf(stderr, "\t-l         Run libc malloc as well.\n");
	fprintf(stderr, "\t-t <dir>   Directory to find default traces.\n");
	fprintf(stderr, "\t-v         Print per-trace performance breakdowns.\n");
	fprintf(stderr, "\t-V         Print additional debug info.\n");
}
