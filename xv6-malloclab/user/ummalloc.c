#include "kernel/types.h"

//
#include "user/user.h"

//
#include "ummalloc.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(uint)))

/* Base constants and macros */
// 参考了书本
#define WSIZE 4				/* Word and header/footer size (bytes) */
#define DSIZE 8				/* Double word size (bytes) */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount (bytes) */

/* Pack a size and allocated bit into a word*/
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = val)
#define SIZE_EQUAL 1
#define SIZE_LESS 2
#define SIZE_GREATER 3
/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define LEFT (size - asize)
/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

int max(int x, int y)
{
	if (x > y)
	{
		return x;
	}
	else
	{
		return y;
	}
}
/* Heap prologue block pointer*/
static void *heap_listp;
static int *coalesce(void *ptr)
{
	uint prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
	uint next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
	uint size = GET_SIZE(HDRP(ptr));

	int condition = (prev_alloc << 1) | next_alloc;

	switch (condition)
	{
	case 3: // 前后都已分配
		return ptr;

	case 2: // 前已分配，后未分配
		size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
		PUT(HDRP(ptr), PACK(size, 0));
		PUT(FTRP(ptr), PACK(size, 0));
		break;

	case 1: // 前未分配，后已分配
		size += GET_SIZE(FTRP(PREV_BLKP(ptr)));
		PUT(FTRP(ptr), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
		ptr = PREV_BLKP(ptr);
		break;

	case 0: // 前后都未分配
		size += GET_SIZE(HDRP(PREV_BLKP(ptr))) + GET_SIZE(FTRP(NEXT_BLKP(ptr)));
		PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
		ptr = PREV_BLKP(ptr);
		break;
	}

	return ptr;
}
// 用作一个指向序言块（prologue block）的指针。
static inline uint adjust_size(uint words)
{
	return (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
}

static void init_block(char *bp, uint size)
{
	PUT(HDRP(bp), PACK(size, 0)); // Free block header
	PUT(FTRP(bp), PACK(size, 0)); // Free block footer
}

static void set_new_epilogue(char *bp, uint size)
{
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // New epilogue header
}

static void *extend_heap(uint words)
{
	char *bp;
	uint size = adjust_size(words);

	if ((long)(bp = sbrk(size)) == -1)
		return 0;

	init_block(bp, size);
	set_new_epilogue(bp, size);

	return coalesce(bp);
}

/*
 * mm_init - initialize the malloc package.
 */
int init()
{
	PUT(heap_listp, 0);							   // Alignment padding
	PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // Prologue header
	PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // Prologue footer
	PUT(heap_listp + (3 * WSIZE), PACK(0, 1));	   // Epilogue header
	heap_listp += (2 * WSIZE);

	if (extend_heap(CHUNKSIZE / WSIZE) == 0)
		return 0;
	return 1;
}

int mm_init(void)
{
	/* Create the initial empty heap */
	if ((heap_listp = sbrk(4 * WSIZE)) == (void *)-1)
		return -1;
	if (init() == 0)
		return -1; // we failed
	return 0;	   /// success
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
static void *find_fit(uint asize)
{
	void *p;

	for (p = heap_listp; GET_SIZE(HDRP(p)) > 0; p = NEXT_BLKP(p))
	{
		if (!GET_ALLOC(HDRP(p)) && (asize <= GET_SIZE(HDRP(p))))
			return p;
	}

	return 0;
}
static void place(void *bp, uint asize)
{
	uint size = GET_SIZE(HDRP(bp));
	if (LEFT >= 2 * WSIZE)
	{
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(LEFT, 0));
		PUT(FTRP(bp), PACK(LEFT, 0));
	}
	else
	{
		PUT(HDRP(bp), PACK(size, 1));
		PUT(FTRP(bp), PACK(size, 1));
	}
}

uint adjust(uint size)
{
	if (size <= DSIZE)
	{
		return 2 * DSIZE;
	}
	else
	{
		return DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
	}
}

void *mm_malloc(uint size)
{
	uint asize;		 /* Adjusted block size */
	uint extendsize; /* Amount to extend heap if no fit */
	char *bp;

	/* Ignore spurious requests */
	if (!size)
	{
		return 0;
	}

	/* Adjust block size to include overhead and alignment reqs. */
	asize = adjust(size);

	/* Search the free list for a fit */
	if ((bp = find_fit(asize)) != 0)
	{
		place(bp, asize);
		return bp;
	}

	/* No fit found. Get more memory and place the block */
	extendsize = max(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == 0)
	{
		return 0;
	}
	place(bp, asize);

	return bp;
}

/* find_fit -- find the first fit free block */

/* place when remaining part size is greater than 2 word, divide it. */

/*
 * mm_free - Freeing a block does nothing.
 */
/*
 * mm_free - Freeing a block and coalesce free block if can .
 */
void mm_free(void *ptr)
{
	uint size = GET_SIZE(HDRP(ptr));

	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));
	coalesce(ptr);
}

/* coalesce -- Coalesce free block */

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
// 声明用于处理内存分配和重新分配的函数
void *handle_size_greater(void *ptr, uint asize, uint blockSize);
void *mm_realloc(void *ptr, uint size)
{

	uint blockSize;
	uint asize;
	/* Adjusted block size */

	int caseType;
	if (ptr == 0)
	{ /* If ptr == NULL, call mm_alloc(size) */
		return mm_malloc(size);
	}
	else if (size == 0)
	{ /* If size == 0, call mm_free(size) */
		mm_free(ptr);
		return 0;
	}

	asize = adjust(size); /* Adjust block size to include overhead and alignment reqs. */

	blockSize = GET_SIZE(HDRP(ptr));
	if (asize == blockSize)
	{
		caseType = SIZE_EQUAL;
	}
	else if (asize < blockSize)
	{
		caseType = SIZE_LESS;
	}
	else
	{
		caseType = SIZE_GREATER;
	}
	switch (caseType)
	{
	case SIZE_EQUAL:
		// 块大小相等，无需做任何事
		return ptr;

	case SIZE_LESS:
		// 新大小小于当前块大小，调整当前块
		place(ptr, asize);
		return ptr;

	case SIZE_GREATER:
		// 新大小大于当前块大小，尝试合并或重新分配
		return handle_size_greater(ptr, asize, blockSize);

	default:
		// 不应该到达这里
		return 0;
	}
}
void *handle_size_greater(void *ptr, uint asize, uint blockSize) {
    void *newptr;
    void *nextptr = NEXT_BLKP(ptr);
    uint sizesum = GET_SIZE(HDRP(nextptr)) + blockSize;

    if (!GET_ALLOC(HDRP(nextptr)) && sizesum >= asize) {
        PUT(HDRP(ptr), PACK(sizesum, 0)); // 合并块
        place(ptr, asize);
        return ptr;
    } else {
        uint extendsize;
        newptr = find_fit(asize);
        if (newptr == 0) {
            extendsize = max(asize, CHUNKSIZE);
            newptr = extend_heap(extendsize / WSIZE);
            if (newptr == 0)
                return 0;
        }
        place(newptr, asize);
        memcpy(newptr, ptr, blockSize - 2 * WSIZE); // 移动数据到新块
        mm_free(ptr);
        return newptr;
    }
}