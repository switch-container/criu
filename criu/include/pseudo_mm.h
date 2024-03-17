#ifndef __PSEUDO_MM_H__
#define __PSEUDO_MM_H__

#include "types.h"
#include "pseudo_mm_ioctl.h"

#define PSEUDO_MM_INHERIT_ID "pseudo-mm-drv"
enum {
	PSEUDO_MM_RDMA_BUF_SOCK_MAP = 0x1,
};

/*
 * Create a new pseudo_mm.
 *
 * @drv_fd: the file descriptor of /dev/pseudo_mm driver
 * @id: The pointer used for save the id of newly created pseudo_mm.
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_create(int drv_fd, int *id);

/*
 * Delete a pseudo_mm.
 *
 * @drv_fd: the file descriptor of /dev/pseudo_mm driver
 * @id: The id of the pseudo_mm to be deleted. (Pass -1 means delete all)
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_delete(int drv_fd, int id);

/*
 * Register the dax device to the pseudo_mm module in kernel.
 * (Should only be called once globally)
 *
 * @drv_fd: the file descriptor of /dev/pseudo_mm driver
 * @fd: the file descriptor of the dax device (typically CXL-mem device or PMEM
 * device)
 *
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_register(int drv_fd, int fd);

/*
 * Add a mmap to an existing pseudo_mm.
 *
 * @drv_fd: the file descriptor of /dev/pseudo_mm driver
 * @id: the id of the target pseudo_mm.
 * @other params: the same as mmap().
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_add_map(int drv_fd, int id, void *start, size_t len, int prot, int flags, int fd, off_t offset);

/*
 * Setup page table of an memory area, pointing to the registered dax device.
 *
 * @drv_fd: the file descriptor of /dev/pseudo_mm driver
 * @id: the id of the target pseudo_mm.
 * @start: the start virtual address of area that needed setup page table.
 * @len: the length of area
 * @pgoff: the page offset on dax device.
 * @type: the type of the backend, currently support DAX_MEM and RDMA_MEM only
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_setup_pt(int drv_fd, int id, void *start, size_t len, unsigned long pgoff, enum pseudo_mm_pt_type type);

/*
 * Attach an existing pseudo_mm into a process.
 *
 * @drv_fd: the file descriptor of /dev/pseudo_mm driver
 * @id: the id of the pseudo_mm.
 * @pid: the target process.
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_attach(int drv_fd, int id, pid_t pid);

/*
 * Bring back the physical page located on CXL memory into local memory.
 * This method is mainly used for memory hierarchy.
 *
 * @drv_fd: the file descriptor of /dev/pseudo_mm driver
 * @id: the id of the pseudo_mm
 * @start: the start virtual address that need to bring back local memory
 * @len: the length of memory that need to bring back (must be 4K-aligned)
 *
 * Return non-zero if error occurs, otherwise return 0.
 */
int pseudo_mm_bring_back(int drv_fd, int id, void *start, size_t len);
#endif
