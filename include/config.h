#ifndef CONFIG_H
#define CONFIG_H

/* Memory alignment (must be power of 2) */
#define ALIGNMENT 16

/* Word and double word sizes (bytes) */
#define WSIZE 8   /* Word size: header/footer size */
#define DSIZE 16  /* Double word size */

/* Extend heap by this amount (bytes) */
#define CHUNKSIZE (1 << 12) /* 4096 bytes */

/* Minimum block size: header + footer = 16 bytes minimum */
#define MIN_BLOCK_SIZE (2 * DSIZE)

/* Maximum heap size (64 MB) */
#define MAX_HEAP_SIZE (1 << 26)

/* Alignment macros */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

#endif /* CONFIG_H */
