#ifndef __CR_CONVERT_H__
#define __CR_CONVERT_H__

#include "asm-generic/int.h"
#include "images/pagemap.pb-c.h"

#define PSEUDO_MM_ID_FILE_TEMPLATE "pseudo_mm_id-%ld"

struct convert_ctl {
	int (*advance)(struct convert_ctl *pr);
	void (*skip_pages)(struct convert_ctl *, unsigned long len);
	void (*close)(struct convert_ctl *);

	int dax_dev_fd;
	unsigned long dax_pgoff; /* page offset within the dax devices */
	int pseudo_mm_id;	 /* current pseudo_mm_id, <0 means invalid */

	struct cr_img *pmi; /* pagemap img */
	struct cr_img *pi;  /* pages img */
	u32 pages_img_id;   /* pages-<ID>.img file ID */

	PagemapEntry *pe;     /* current pagemap we are on */
	unsigned long img_id; /* pagemap image file ID */

	PagemapEntry **pmes;
	int nr_pmes;
	int curr_pme;
};

/* Return 0 when succeed */
extern int cr_convert(void);
#endif
