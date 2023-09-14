#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "log.h"
#include "servicefd.h"
#include "pseudo_mm.h"

int pseudo_mm_create(int *id)
{
	int drv_fd = get_service_fd(PSEUDO_MM_DRIVER_OFF);
	if (drv_fd < 0)
		return -1;
	return ioctl(drv_fd, PSEUDO_MM_IOC_CREATE, (void *)id);
}

int pseudo_mm_delete(int id)
{
	int drv_fd = get_service_fd(PSEUDO_MM_DRIVER_OFF);
	if (drv_fd < 0)
		return -1;
	return ioctl(drv_fd, PSEUDO_MM_IOC_DELETE, (void *)&id);
}

int pseudo_mm_register(int fd)
{
	int drv_fd = get_service_fd(PSEUDO_MM_DRIVER_OFF);
	if (drv_fd < 0)
		return -1;
	return ioctl(drv_fd, PSEUDO_MM_IOC_REGISTER, (void *)&fd);
}

int pseudo_mm_add_map(int id, void *start, size_t len, int prot, int flags, int fd, off_t offset)
{
	struct pseudo_mm_add_map_param param = {
		.id = id,
		.start = (unsigned long)start,
		.end = (unsigned long)start + (unsigned long)len,
		.prot = (unsigned long)prot,
		.flags = (unsigned long)flags,
		.fd = fd,
		.offset = offset,
	};
	int drv_fd = get_service_fd(PSEUDO_MM_DRIVER_OFF);
	if (drv_fd < 0)
		return -1;
	return ioctl(drv_fd, PSEUDO_MM_IOC_ADD_MAP, (void *)&param);
}

int pseudo_mm_setup_pt(int id, void *start, size_t len, unsigned long pgoff)
{
	struct pseudo_mm_setup_pt_param param = {
		.id = id,
		.start = (unsigned long)start,
		.size = (unsigned long)len,
		.pgoff = pgoff,
	};
	int drv_fd = get_service_fd(PSEUDO_MM_DRIVER_OFF);
	if (drv_fd < 0)
		return -1;
	return ioctl(drv_fd, PSEUDO_MM_IOC_SETUP_PT, (void *)&param);
}

int cr_pseudo_mm_init(void)
{
	int fd;
	int ret;
	fd = open(PSEUDO_MM_DRIVER, O_RDWR);
	if (fd < 0) {
		pr_perror("open " PSEUDO_MM_DRIVER " failed\n");
		return -1;
	}
	ret = install_service_fd(PSEUDO_MM_DRIVER_OFF, fd);
	if (ret < 0)
		return -1;
	return 0;
}
