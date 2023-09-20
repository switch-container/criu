#ifndef __PSEUDO_MM_H__
#define __PSEUDO_MM_H__

#include "types.h"
#include "pseudo_mm_ioctl.h"

#define PSEUDO_MM_INHERIT_ID "pseudo-mm-drv"

/*
 * Create a new pseudo_mm.
 *
 * @id: The pointer used for save the id of newly created pseudo_mm.
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_create(int *id);

/*
 * Delete a pseudo_mm.
 *
 * @id: The id of the pseudo_mm to be deleted. (Pass -1 means delete all)
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_delete(int id);

/*
 * Register the dax device to the pseudo_mm module in kernel.
 * (Should only be called once globally)
 *
 * @fd: the file descriptor of the dax device (typically CXL-mem device or PMEM
 * device)
 *
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_register(int fd);

/*
 * Add a mmap to an existing pseudo_mm.
 *
 * @id: the id of the target pseudo_mm.
 * @other params: the same as mmap().
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_add_map(int id, void *start, size_t len, int prot, int flags,
                      int fd, off_t offset);

/*
 * Setup page table of an memory area, pointing to the registered dax device.
 *
 * @id: the id of the target pseudo_mm.
 * @start: the start virtual address of area that needed setup page table.
 * @len: the length of area
 * @pgoff: the page offset on dax device.
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_setup_pt(int id, void *start, size_t len, unsigned long pgoff);

/*
 * Attach an existing pseudo_mm into a process.
 *
 * @fd: the fd of the pseudo_mm driver
 * @id: the id of the pseudo_mm.
 * @pid: the target process.
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_attach(int fd, int id, pid_t pid);

/*
 * Install /dev/pseudo_mm driver fd.
 *
 * NOTE: call this function before switching the mnt namespace
 */
int cr_pseudo_mm_init(void);

#endif
