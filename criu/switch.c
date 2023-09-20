#include "action-scripts.h"
#include <unistd.h>
#include <sys/wait.h>

#include "log.h"
#include "servicefd.h"
#include "cr_options.h"
#include "files.h"
#include "switch.h"
#include "util.h"
#include "namespaces.h"
#include "mount.h"
#include "fdstore.h"
#include "sys/mount.h"
#include "pstree.h"

struct switch_namespace_info switch_namespace[MAX_SWITCH_NS] = {
	[SWITCH_NS_NET] = { .name = "net", .clone_flag = CLONE_NEWNET },
	[SWITCH_NS_MNT] = { .name = "mnt", .clone_flag = CLONE_NEWNS },
	[SWITCH_NS_IPC] = { .name = "ipc", .clone_flag = CLONE_NEWIPC },
	[SWITCH_NS_UTS] = { .name = "uts", .clone_flag = CLONE_NEWUTS },
};

static char original_pwd[256] = { 0 };

int prepare_mnt_ns_for_switch(void)
{
	struct ns_id *nsid;
	int num = 0, mntns_fd, root_fd;
	char *id = SWITCH_NS_KEY_PREFIX "mnt";

	// we need chdir and chroot to the mnt namespace
	if (chdir("/"))
		return -1;
	if (chroot("/"))
		return -1;

	mntns_fd = inherit_fd_lookup_id(id);
	if (mntns_fd < 0) {
		pr_err("lookup inherit fd for %s failed", id);
		return -1;
	}

	root_fd = open_proc(PROC_SELF, "root");
	if (root_fd < 0) {
		pr_perror("open self root failed");
		return -1;
	}

	for (nsid = ns_ids; nsid != NULL; nsid = nsid->next) {
		if (nsid->nd != &mnt_ns_desc) {
			continue;
		}
		num++;
		nsid->mnt.nsfd_id = fdstore_add(mntns_fd);
		nsid->mnt.root_fd_id = fdstore_add(root_fd);
	}

	if (num > 1) {
		pr_err("switch only support one mnt namespace find %d", num);
		return -1;
	}

	// finally we remount proc fs
	if (mount("proc", "/proc", "proc", MS_MGC_VAL | MS_NOSUID | MS_NOEXEC, NULL)) {
		pr_perror("mount proc fs failed");
		return -1;
	}

	return 0;
}

static int fill_inherit_switch_ns_key(int switch_ns_type, char *buf)
{
	if (switch_ns_type >= MAX_SWITCH_NS) {
		// do not find
		return 1;
	}

	sprintf(buf, SWITCH_NS_KEY_PREFIX "%s", switch_namespace[switch_ns_type].name);
	return 0;
}

int join_switch_namespace(void)
{
	// PID namespace is specical, after the init process of an existing pid namespace been killed
	// then this pid namespace cannot create any new process (e.g, using fork()).
	// Any call to `fork()` will return ENOMEM (cannot allocate memory)
	char id[32];
	int target_ns_fd;
	int res;

	for (int i = 0; i < MAX_SWITCH_NS; i++) {
		if (fill_inherit_switch_ns_key(i, id)) {
			return 1;
		}
		target_ns_fd = inherit_fd_lookup_id(id);
		if (target_ns_fd < 0) {
			continue;
		}

		res = setns(target_ns_fd, switch_namespace[i].clone_flag);
		if (res != 0) {
			pr_err("fail to switch to namespace id = %s", id);
			return 1;
		}
	}

	return 0;
}

int stash_criu_original_mntns(int *fd_id)
{
	// for now we only need stash mnt ns (and root dir)
	int fd, proc_fd;

	if ((root_ns_mask & CLONE_NEWNS) == 0)
		return 0;

	proc_fd = get_service_fd(CR_PROC_FD_OFF);
	if (proc_fd < 0) {
		pr_err("cannot get proc fd");
		return 1;
	}

	fd = openat(proc_fd, "self/ns/mnt", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		pr_perror("get original mnt ns failed");
		return -1;
	}
	*fd_id = fdstore_add(fd);
	if (*fd_id < 0) {
		return -1;
	}

	close_safe(&fd);

	if (getcwd(original_pwd, sizeof(original_pwd)) == NULL) {
		pr_perror("getcwd failed");
		return -1;
	}

	return 0;
}

int stash_pop_criu_original_mntns(int fd_id)
{
	int fd;
	if (original_pwd[0] == 0) {
		// no need to pop
		return 0;
	}
	fd = fdstore_get(fd_id);
	if (setns(fd, CLONE_NEWNS)) {
		pr_perror("set ns back to original mnt ns failed");
		return 1;
	}
	if (chdir(original_pwd)) {
		pr_perror("chdir to original pwd failed");
		return 1;
	}

	return 0;
}

int detach_root_task(int root_seized)
{
	int pid, status;

	if (root_seized && task_alive(root_item)) {
		pid = root_item->pid->real;

		if (ptrace(PTRACE_INTERRUPT, pid, 0, 0)) {
			pr_perror("Can't interrupt the %d task", pid);
			return -1;
		}

		if (wait4(pid, &status, __WALL, NULL) != pid) {
			pr_perror("waitpid(%d) failed", pid);
			return -1;
		}

		if (ptrace(PTRACE_DETACH, root_item->pid->real, NULL, 0)) {
			pr_perror("deatch root seized failed");
			return -1;
		}
	}

	return 0;
}

int check_skip_action_scripts_in_switch(enum script_actions action)
{
	switch (action) {
	case ACT_PRE_RESTORE:
	case ACT_SETUP_NS:
	case ACT_POST_SETUP_NS:
	case ACT_PRE_RESUME:
	case ACT_POST_RESUME:
		return 1;
	default:
		// to be honest, we only support ACT_POST_RESTORE in switch mode
		return 0;
	}
}
