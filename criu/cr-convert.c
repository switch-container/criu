/*
 * This is a converter, which used to convert the original
 * checkpoint image generated from `dump` command into
 * pseudo_mm (which is a new kernel interface).
 */
#include "include/fdstore.h"
#include "include/namespaces.h"
#include "include/switch.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/stat.h>

#include "types.h"
#include "cr_options.h"
#include "pstree.h"
#include "log.h"
#include "rst_info.h"
#include "files.h"
#include "mem.h"
#include "cr-convert.h"
#include "pagemap.h"
#include "crtools.h"
#include "restorer.h"
#include "pseudo_mm.h"

#include "protobuf.h"
#include "image-desc.h"
#include "images/pagemap.pb-c.h"
#include "images/mm.pb-c.h"
#include "images/pstree.pb-c.h"

#undef LOG_PREFIX
#define LOG_PREFIX "converter: "

static int mmap_pages_img(struct convert_ctl *cc)
{
	int ret;
	struct stat stat_buf;
	size_t img_size;
	void *addr;

	ret = fstat(cc->pi->fd, &stat_buf);
	if (ret) {
		pr_perror("fstat pages img failed");
		return -1;
	}
	img_size = stat_buf.st_size;
	if (img_size & (PAGE_SIZE - 1)) {
		pr_err("pages-%d.img's size not page align %ld\n", cc->pages_img_id, img_size);
		return -1;
	}
	addr = mmap(NULL, img_size, PROT_READ | PROT_WRITE, MAP_SHARED, cc->dax_dev_fd, cc->dax_pgoff << PAGE_SHIFT);
	if (!addr) {
		pr_perror("mmap dax device failed");
		return -1;
	}
	ret = read_img_buf(cc->pi, addr, img_size);
	if (ret < 0)
		return -1;
	ret = munmap(addr, img_size);
	if (ret) {
		pr_perror("unmap dax device area failed");
		return -1;
	}
	cc->nr_pages_mmap += img_size >> PAGE_SHIFT;
	pr_debug("map pages-%d.img to dax device off %#lx\n", cc->pages_img_id, cc->dax_pgoff << PAGE_SHIFT);
	return 0;
}

static int generate_pseudo_mm_img(struct convert_ctl *cc)
{
	char path_buf[128];
	FILE *pseudo_mm_file;
	int img_dir_fd = get_service_fd(IMG_FD_OFF);

	pr_info("Start generate pseudo_mm-%ld img at %s...\n", cc->img_id, opts.imgs_dir);
	sprintf(path_buf, PSEUDO_MM_ID_FILE_TEMPLATE, cc->img_id);
	pseudo_mm_file = fopenat(img_dir_fd, path_buf, "w");
	if (!pseudo_mm_file) {
		pr_err("Cannot open %s\n", path_buf);
		return -1;
	}
	fprintf(pseudo_mm_file, "%d", cc->pseudo_mm_id);
	fclose(pseudo_mm_file);

	return 0;
}

static int generate_pages_num_img(struct convert_ctl *cc)
{
	FILE *pseudo_mm_file;
	int img_dir_fd = get_service_fd(IMG_FD_OFF);

	pseudo_mm_file = fopenat(img_dir_fd, CONVERT_PAGE_NUM_IMG, "w");
	if (!pseudo_mm_file) {
		pr_err("Cannot open " CONVERT_PAGE_NUM_IMG "\n");
		return -1;
	}
	fprintf(pseudo_mm_file, "%d", cc->nr_pages_mmap);
	fclose(pseudo_mm_file);

	return 0;
}

int prepare_pseudo_mm_id(int vpid, struct task_restore_args *ta)
{
	char path_buf[PATH_MAX];
	FILE *pseudo_mm_file;
	int img_dir = get_service_fd(IMG_FD_OFF);
	int pseudo_mm_id;

	sprintf(path_buf, PSEUDO_MM_ID_FILE_TEMPLATE, (unsigned long)vpid);
	pseudo_mm_file = fopenat(img_dir, path_buf, "r");
	if (!pseudo_mm_file) {
		pr_err("Cannot open %s\n", path_buf);
		return -1;
	}
	if (fscanf(pseudo_mm_file, "%d", &pseudo_mm_id) < 0) {
		pr_perror("invalid pseudo_mm_file");
		return -1;
	}
	fclose(pseudo_mm_file);
	if (pseudo_mm_id <= 0) {
		return -1;
	}
	ta->pseudo_mm_id = pseudo_mm_id;
	return 0;
}

int convert_one_task(struct pstree_item *item, struct convert_ctl *cc)
{
	int ret;

	ret = pseudo_mm_create(cc->pseudo_mm_drv_fd, &cc->pseudo_mm_id);
	if (ret) {
		pr_err("create pseudo_mm failed\n");
		return ret;
	}

	// TODO(huang-jl) how to handle file-backed mapping ?
	// (a) add it into pseudo_mm or (b) restore one by one in CRIU
	// I think the (b) is better, since the overlayfs will changed
	// each time when switching container. And we HOPE the file-backed
	// mapping will point to the file in the overlayfs.
	ret = build_pseudo_mm_for_convert(item, cc);
	if (ret) {
		pr_err("build_pseudo_mm_for_convert() vpid %d failed\n", vpid(item));
		return ret;
	}
	return 0;
}

int convert_one_ctr(struct convert_ctl *cc)
{
	int ret;
	struct pstree_item *item;

	if (check_img_inventory(true)) {
		pr_err("check img inventory at %s failed\n", opts.imgs_dir);
		return -1;
	}

	if (prepare_task_entries()) {
		pr_err("prepare taks entries at %s failed\n", opts.imgs_dir);
		return -1;
	}
	// read files.img
	if (prepare_files()) {
		pr_err("prepare files at %s failed\n", opts.imgs_dir);
		return -1;
	}
	if (prepare_pstree() < 0) {
		pr_err("prepare pstree at %s failed\n", opts.imgs_dir);
		return -1;
	}

	if (mount_proc()) {
		pr_err("mount proc failed\n");
		return -1;
	}
	if (join_switch_namespace()) {
		pr_err("join mnt namesapce failed\n");
		return -1;
	}
	if (root_ns_mask & CLONE_NEWNS) {
		if (prepare_mnt_ns_for_switch()) {
			pr_err("prepare mnt ns for switch failed\n");
			return -1;
		}
	}

	// first prepare vma for each task
	for_each_pstree_item(item) {
		ret = prepare_mm_for_convert(item);
		if (ret) {
			pr_err("prepares_mm_for_convert() vpid %d failed\n", vpid(item));
			return ret;
		}
	}
	prepare_cow_vmas();

	/**************************************
	 * Start Convert
	 **************************************/
	for_each_pstree_item(item) {
		if (open_convert_ctl(vpid(item), cc) <= 0)
			return -1;
		// mmap to dax device
		if (mmap_pages_img(cc)) {
			pr_err("fill dax device with pages-%d.img failed\n", cc->pages_img_id);
			return -1;
		}
		ret = convert_one_task(item, cc);
		if (ret)
			return ret;
		// here we get the new pseudo_mm_id for `item`
		pr_info("convert task (vpid %d) to pseudo_mm %d\n", vpid(item), cc->pseudo_mm_id);
		// write a file used for pseudo_mm_attach when restore
		ret = generate_pseudo_mm_img(cc);
		if (ret)
			return ret;
		cc->close(cc);
	}
	ret = generate_pages_num_img(cc);
	if (ret)
		return ret;
	// clean up
	root_item = NULL;
	return 0;
}

/*
 * For simplicity, do not consider any cleanup for now.
 * (e.g., file descriptor and pstree item...)
 */
int cr_convert(void)
{
	int ret, dax_dev_fd;
	// how many pages located on dax device
	// only initialize necessary param
	struct convert_ctl cc = { .dax_pgoff = opts.dax_pgoff, .nr_pages_mmap = 0 };

	if (fdstore_init()) {
		pr_err("fdstore init failed\n");
		return -1;
	}
	if (inherit_fd_move_to_fdstore()) {
		pr_err("inherit fd move to fdstore failed\n");
		return -1;
	}
	cc.pseudo_mm_drv_fd = inherit_fd_lookup_id(PSEUDO_MM_INHERIT_ID);
	if (cc.pseudo_mm_drv_fd < 0) {
		pr_err("cannot find " PSEUDO_MM_INHERIT_ID " in inherit fd list\n");
		return -1;
	}
	if (!opts.dax_device) {
		pr_err("Must specify --dax-device for convert!\n");
		return -1;
	}
	dax_dev_fd = open(opts.dax_device, O_RDWR);
	if (dax_dev_fd < 0) {
		pr_perror("Cannot open dax device %s", opts.dax_device);
		return -1;
	}
	cc.dax_dev_fd = dax_dev_fd;
	ret = pseudo_mm_register(cc.pseudo_mm_drv_fd, dax_dev_fd);
	if (ret) {
		pr_perror("Cannot register dax device for pseudo_mm!");
		return -1;
	}

	pr_debug("register pseudo_mm with %s\n", opts.dax_device);

	ret = convert_one_ctr(&cc);
	if (ret)
		return ret;
	pr_debug("Finish cr convert at %s\n", opts.imgs_dir);
	return 0;
}
