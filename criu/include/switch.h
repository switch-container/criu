#ifndef __CR_SWITCH_H__
#define __CR_SWITCH_H__

#include "action-scripts.h"
#include <sched.h>

#define SWITCH_NS_KEY_PREFIX "switch-ns-"

struct switch_namespace_info {
	char *name;
	int clone_flag;
};

enum {
	SWITCH_NS_NET,
	SWITCH_NS_MNT,
	SWITCH_NS_IPC,
	SWITCH_NS_UTS,

	MAX_SWITCH_NS,
};

// return 1 if can skip
// return 0 if cannot skip
extern int check_skip_action_scripts_in_switch(enum script_actions action);

extern struct switch_namespace_info switch_namespace[MAX_SWITCH_NS];
extern int prepare_mnt_ns_for_switch(void);
extern int join_switch_namespace(void);
/*
 * this is a workaroud for unmounting cgroup yard for now
 * @fd_id: to store the valud of id in fdstore
 */
extern int stash_criu_original_mntns(int *fd_id);
extern int stash_pop_criu_original_mntns(int fd_id);

extern int detach_root_task(int root_seized);

#endif
