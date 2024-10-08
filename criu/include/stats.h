#ifndef __CR_STATS_H__
#define __CR_STATS_H__
#include <sys/time.h>

enum {
	TIME_FREEZING,
	TIME_FROZEN,
	TIME_MEMDUMP,
	TIME_MEMWRITE,
	TIME_IRMAP_RESOLVE,

	DUMP_TIME_NR_STATS,
};

enum {
	TIME_FORK,
	TIME_RESTORE,
	TIME_RESTORE_PREPARE_NS,
	// root task restore shared resource (e.g., mm )
	TIME_RESTORE_PREPARE_SHARED,
	TIME_RESTORE_PREPARE_CGROUP,
	TIME_AFTER_RESTORE,

	RESTORE_TIME_NS_STATS,
};

extern const char* RESTORE_TIME_MAP [RESTORE_TIME_NS_STATS];

extern void timing_start(int t);
extern void timing_stop(int t);

enum {
	CNT_PAGES_SCANNED,
	CNT_PAGES_SKIPPED_PARENT,
	CNT_PAGES_WRITTEN,
	CNT_PAGES_LAZY,
	CNT_PAGE_PIPES,
	CNT_PAGE_PIPE_BUFS,

	CNT_SHPAGES_SCANNED,
	CNT_SHPAGES_SKIPPED_PARENT,
	CNT_SHPAGES_WRITTEN,

	DUMP_CNT_NR_STATS,
};

enum {
	CNT_PAGES_COMPARED,
	CNT_PAGES_SKIPPED_COW,
	CNT_PAGES_RESTORED,

	RESTORE_CNT_NR_STATS,
};

extern void cnt_add(int c, unsigned long val);
extern void cnt_sub(int c, unsigned long val);

#define DUMP_STATS    1
#define RESTORE_STATS 2

extern int init_stats(int what);
extern void write_stats(int what);
extern void print_restore_timing(void);
extern long timeval_to_us(const struct timeval *tv);
extern const struct timeval *get_timing_start(int t);

#endif /* __CR_STATS_H__ */
