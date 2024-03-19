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
#include "util-pie.h"

#include "protobuf.h"
#include "image-desc.h"
#include "images/pagemap.pb-c.h"
#include "images/mm.pb-c.h"
#include "images/pstree.pb-c.h"
#include "imgset.h"

#undef LOG_PREFIX
#define LOG_PREFIX "converter: "

static int _get_nr_vmas_of_condensed_mm(struct vm_area_list *vmas)
{
	struct vma_area *vma_area;
	int nr_vmas = 0, nr_condensed_vmas = 0;
	unsigned long begin = 0, end = 0;

	list_for_each_entry(vma_area, &vmas->h, list) {
		VmaEntry *vma = vma_area->e;
		if (vma_entry_is(vma, VMA_AREA_VDSO) || vma_entry_is(vma, VMA_AREA_VVAR)) {
			nr_vmas++;
			if (begin != 0 && end != 0)
				nr_condensed_vmas++;
			begin = end = 0;
			continue;
		}
		if (begin == 0) {
			begin = vma->start;
		} else {
			assert(begin < vma->start);
		}
		assert(end < vma->end);
		end = vma->end;
	}
	if (begin != 0 && end != 0)
		nr_condensed_vmas++;

	return nr_vmas + nr_condensed_vmas;
}

static struct vma_area *_new_condensed_vma(unsigned long begin, unsigned long end)
{
	struct vma_area *res;
	res = alloc_vma_area();
	if (res) {
		res->e->start = begin;
		res->e->end = end;
		res->e->pgoff = 0;
		res->e->prot = PROT_NONE;
		res->e->flags = MAP_PRIVATE;
		res->e->status = VMA_AREA_FAKE;
	}
	return res;
}

/*
 * This function will generate a condensed mm image.
 * The schema of image is the same as mm image.
 *
 * The reason I decide to create a condensed mm image is that
 * when restoring, we only need VMA_AREA_VDSO while the other vma
 * is restored by new kernel interface (i.e. pseudo_mm). So it is
 * wasteful to read and parse the complete vma mappings image.
 *
 * [   ========= ====== ======= ======== ... ===    ======= =======     =====  ==         ]
 *     ^ first vma                                  ^ vdso  ^ vvar
 *     *****************************************    ******* *******     *********
 *     ^ This is condense_vma_area                                      ^ This is condense_vma_area
 */
static int dump_converted_task_mm(struct pstree_item *item)
{
	MmEntry mme = MM_ENTRY__INIT;
	// fake_vma_area is two fake area lies between VDSO and VVAR
	struct vma_area *vma_area, *condense_vma_area;
	struct rst_info *ri = rsti(item);
	unsigned long begin = 0, end = 0;
	int i = 0, ret;
	struct cr_img *img;

	mme = *ri->mm;

	// 2-pass to generate condensed mm image:
	// 1. the first pass is used to determine the number of vmas
	// 2. the second pass is used to fill the mme
	mme.n_vmas = _get_nr_vmas_of_condensed_mm(&ri->vmas);
	mme.vmas = xmalloc(mme.n_vmas * sizeof(VmaEntry *));
	if (!mme.vmas)
		return 1;

	pr_debug("convert task %d mm with %ld vmas\n", vpid(item), mme.n_vmas);

	list_for_each_entry(vma_area, &ri->vmas.h, list) {
		VmaEntry *vma = vma_area->e;
		// we only care about VMA_AREA_VDSO and VVAR
		if (vma_entry_is(vma, VMA_AREA_VDSO) || vma_entry_is(vma, VMA_AREA_VVAR)) {
			if (begin != 0 && end != 0) {
				condense_vma_area = _new_condensed_vma(begin, end);
				if (condense_vma_area == NULL)
					return 1;
				mme.vmas[i++] = condense_vma_area->e;
			}
			begin = end = 0;
			mme.vmas[i++] = vma;
			continue;
		}
		if (begin == 0)
			begin = vma->start;
		end = vma->end;
	}

	if (begin != 0 && end != 0) {
		condense_vma_area = _new_condensed_vma(begin, end);
		if (condense_vma_area == NULL)
			return 1;
		mme.vmas[i++] = condense_vma_area->e;
	}
	img = open_image(CR_FD_CONDENSE_MM, O_DUMP, vpid(item));
	if (!img) {
		pr_err("open image CONDENSE_MM failed\n");
		return 1;
	}
	ret = pb_write_one(img, &mme, PB_MM);
	if (ret) {
		pr_err("write mm entry failed!\n");
		return 1;
	}
	close_image(img);

	xfree(mme.vmas);
	return 0;
}

static int prepare_rdma_buf_sock(struct convert_ctl *cc)
{
	int len, sock_fd;
	char *uds_path = opts.rdma_buf_sock_path;
	struct sockaddr_un saddr;

	// prepare addr
	memset(&saddr, 0, sizeof(struct sockaddr_un));
	saddr.sun_family = AF_UNIX;
	len = snprintf(saddr.sun_path, sizeof(saddr.sun_path), "%s", uds_path);
	if (len >= sizeof(saddr.sun_path)) {
		pr_err("Wrong UNIX socket name: %s\n", uds_path);
		return -1;
	}
	// connect
	if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		pr_perror("cretae socket failed");
		return -1;
	}
	len = offsetof(struct sockaddr_un, sun_path) + strlen(saddr.sun_path);
	if (connect(sock_fd, (struct sockaddr *)&saddr, len) < 0) {
		pr_perror("connect to %s failed", saddr.sun_path);
		close(sock_fd);
		return -1;
	}
	cc->buf_sock_fd = sock_fd;
	return 0;
}

static int mmap_pages_img_to_rdma_server(struct convert_ctl *cc)
{
	// first send a command
	// then send the fd and pgoff
	int buf = PSEUDO_MM_RDMA_BUF_SOCK_MAP;
	int ret;
	struct stat stat_buf;
	size_t img_size;

	// make sure image size is correct
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

	ret = send(cc->buf_sock_fd, &buf, sizeof(buf), 0);
	if (ret < 0) {
		pr_perror("send cmd %d to buf sock failed", buf);
		return -1;
	}
	ret = send_fds(cc->buf_sock_fd, NULL, 0, &cc->pi->fd, 1, &cc->rdma_pgoff, sizeof(cc->rdma_pgoff));
	if (ret != 0) {
		pr_perror("send fds on buf_sock failed");
		return -1;
	}
	// we need sync with remote server in case that it did not
	// recv fds but we close the connection.
	ret = recv(cc->buf_sock_fd, &buf, sizeof(buf), 0);
	if (ret != sizeof(buf) || buf != 0) {
		pr_err("recv ack from buf socket failed!\n");
		return -1;
	}
	pr_debug("map pages-%d.img to rdma off %#lx\n", cc->pages_img_id, cc->rdma_pgoff << PAGE_SHIFT);
	cc->nr_pages_mmap += img_size >> PAGE_SHIFT;
	return 0;
}

static int mmap_pages_img_to_dax(struct convert_ctl *cc)
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
		// NOTE by huang-jl: this two function (mmap_pages_img_to_xxx) will both
		// modify cc->nr_pages_mmap. If in the future, you wants to mmap to both
		// memory pool, please only update cc->nr_pages_mmap once!
		switch (cc->mem_pool_type) {
		case DAX_MEM_POOL:
			// mmap to dax device
			if (mmap_pages_img_to_dax(cc)) {
				pr_err("fill dax device with pages-%d.img failed\n", cc->pages_img_id);
				return -1;
			}
			break;
		case RDMA_MEM_POOL:
			if (mmap_pages_img_to_rdma_server(cc)) {
				pr_err("fill rdma server with pages-%d.img failed\n", cc->pages_img_id);
				return -1;
			}
			break;
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
		ret = dump_converted_task_mm(item);
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
	struct convert_ctl cc = { .dax_pgoff = opts.dax_pgoff,
				  .nr_pages_mmap = 0,
				  .rdma_pgoff = opts.rdma_pgoff,
				  .mem_pool_type = opts.mem_pool_type };

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
	switch (cc.mem_pool_type) {
	case DAX_MEM_POOL:
		if (!opts.dax_device) {
			pr_err("Must specify --dax-device\n");
			return -1;
		}
		pr_debug("convert using dax_device %s and dax memory pool\n", opts.dax_device);
		dax_dev_fd = open(opts.dax_device, O_RDWR);
		if (dax_dev_fd < 0) {
			pr_perror("Cannot open dax device %s", opts.dax_device);
			return -1;
		}
		ret = pseudo_mm_register(cc.pseudo_mm_drv_fd, dax_dev_fd);
		if (ret) {
			pr_perror("Cannot register dax device for pseudo_mm!");
			return -1;
		}
		cc.dax_dev_fd = dax_dev_fd;
		break;
	case RDMA_MEM_POOL:
		if (!opts.rdma_buf_sock_path) {
			pr_err("Must specify --rdma-buf-sock-path\n");
			return -1;
		}
		ret = prepare_rdma_buf_sock(&cc);
		if (ret) {
			pr_perror("Cannot prepare rdma buf socket!");
			return -1;
		}
		pr_debug("convert using rdma memory pool, rdma_buf_sock_path %s\n", opts.rdma_buf_sock_path);
		break;
	default:
		pr_err(" invalid memory pool type: %d\n", opts.mem_pool_type);
		return 1;
	}

	ret = convert_one_ctr(&cc);
	if (ret)
		return ret;
	pr_debug("Finish cr convert at %s\n", opts.imgs_dir);
	return 0;
}
