#ifndef __PSEUDO_MM_IOCTL_H__
#define __PSEUDO_MM_IOCTL_H__

#include <linux/ioctl.h>
#include "types.h"

#define PSEUDO_MM_IOC_MAGIC 0x1c

enum pseudo_mm_pt_type {
	DAX_MEM = 0x0,
	RDMA_MEM = 0x1,
};

struct pseudo_mm_add_map_param {
	int id;
	unsigned long start;
	unsigned long end;
	unsigned long prot;
	unsigned long flags;
	int fd;
	/* offset should be multiply of PAGE_SIZE (same as mmap) */
	off_t offset;
};

struct pseudo_mm_setup_pt_param {
	int id;
	/* start virtual address */
	unsigned long start;
	/* size of memory area needed to be setup */
	unsigned long size;
	/* page offset in dax device (e.g., 1 means 1 * PAGE_SIZE) */
	unsigned long pgoff;
	/* page table entry types */
	enum pseudo_mm_pt_type type;
};

struct pseudo_mm_bring_back_param {
	int id;
	/* start virtual address */
	unsigned long start;
	/* size of memory area needed to be bring back */
	unsigned long size;
};

struct pseudo_mm_attach_param {
	pid_t pid;
	int id;
};

/* argument is a fd used to identify the backend dax device */
#define PSEUDO_MM_IOC_REGISTER _IOW(PSEUDO_MM_IOC_MAGIC, 0x00, int *)
/* argument is used to RECV pseudo_mm_id */
#define PSEUDO_MM_IOC_CREATE _IOR(PSEUDO_MM_IOC_MAGIC, 0x01, int *)
/* argument is a pseudo_mm_id */
#define PSEUDO_MM_IOC_DELETE _IOW(PSEUDO_MM_IOC_MAGIC, 0x02, int *)
#define PSEUDO_MM_IOC_ADD_MAP \
	_IOW(PSEUDO_MM_IOC_MAGIC, 0x03, struct pseudo_mm_add_anon_param *)
#define PSEUDO_MM_IOC_SETUP_PT \
	_IOW(PSEUDO_MM_IOC_MAGIC, 0x04, struct pseudo_mm_setup_pt_param *)
#define PSEUDO_MM_IOC_ATTACH \
	_IOW(PSEUDO_MM_IOC_MAGIC, 0x05, struct pseudo_mm_attach_param *)
#define PSEUDO_MM_IOC_BRING_BACK \
	_IOW(PSEUDO_MM_IOC_MAGIC, 0x06, struct pseudo_mm_bring_back_param *)

#endif
