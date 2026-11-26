// ksu_module_exports.c — KernelSU を LKM(=m) で使うための symbol export
//
// GKI 組込(CONFIG_KSU=y)時代は link 時に解決されていた KSU driver の
// 必要 symbol を、LKM では export しておく必要がある。本ファイルで
// EXPORT_SYMBOL_GPL する(comet 自前 GKI のみで使う。upstream では該当
// symbol は ABI symbol list で供給される)。
// symbol 一覧は 2026-09-29 の modpost 失敗出力から抽出。
//
// selinux 内部 kernel 籠器(security/selinux 配下)は公式には公開 header 無し
// のため -I に security/selinux{,/include} と fs/ext4 を渡している。
// ただし include/security.h 等が依存する flask.h / av_permissions.h は
// ビルド時生成なので、genheaders(scripts/selinux/genheaders)の出力を
// include 配下に固定投入してある(本 repo 内で再生成不要)。
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/cred.h>
#include <linux/seccomp.h>
#include <linux/mm_types.h>
#include <linux/uaccess.h>
#include <linux/sched/user.h>
#include <linux/anon_inodes.h>
#include <linux/kallsyms.h>
#include <linux/task_work.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/proc_ns.h>
#include <trace/events/syscalls.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <linux/mount.h>
#include <linux/fs_struct.h>

// selinux 内部
#include "ss/avtab.h"
#include "ss/policydb.h"
#include "ss/sidtab.h"
#include "ss/hashtab.h"
#include "ss/symtab.h"
#include "avc.h"
#include "avc_ss.h"
#include "objsec.h"
#include "security.h"
#include "ext4.h"		// ext4_unregister_sysfs(-Ifs/ext4)

// path_mount/path_umount は fs/internal.h(kernel 内部)にしか無い
int path_mount(const char *dev_name, struct path *path, const char *fsname, unsigned long flags, void *data);
int path_umount(struct path *path, int flags);

// arm64 syscall wrapper(SYSCALL_DEFINE が生成る公開無し symbol)
asmlinkage long __arm64_sys_close(const struct pt_regs *regs);
asmlinkage long __arm64_sys_setns(const struct pt_regs *regs);

EXPORT_SYMBOL_GPL(alloc_uid);
EXPORT_SYMBOL_GPL(anon_inode_getfile_secure);
EXPORT_SYMBOL_GPL(__arm64_sys_close);
EXPORT_SYMBOL_GPL(__arm64_sys_setns);
EXPORT_SYMBOL_GPL(avc_has_perm);
EXPORT_SYMBOL_GPL(avc_ss_reset);
EXPORT_SYMBOL_GPL(avtab_alloc);
EXPORT_SYMBOL_GPL(avtab_destroy);
EXPORT_SYMBOL_GPL(avtab_insert_nonunique);
EXPORT_SYMBOL_GPL(avtab_search_node);
EXPORT_SYMBOL_GPL(avtab_search_node_next);
EXPORT_SYMBOL_GPL(change_pid);
EXPORT_SYMBOL_GPL(copy_to_kernel_nofault);
EXPORT_SYMBOL_GPL(dcache_clean_inval_poc);
EXPORT_SYMBOL_GPL(ebitmap_get_bit);
EXPORT_SYMBOL_GPL(ebitmap_set_bit);
EXPORT_SYMBOL_GPL(ext4_unregister_sysfs);
EXPORT_SYMBOL_GPL(__hashtab_insert);
EXPORT_SYMBOL_GPL(init_mm);
EXPORT_SYMBOL_GPL(kallsyms_lookup);
EXPORT_SYMBOL_GPL(kallsyms_lookup_name);
EXPORT_SYMBOL_GPL(kallsyms_lookup_size_offset);
EXPORT_SYMBOL_GPL(kallsyms_on_each_symbol);
EXPORT_SYMBOL_GPL(ksys_unshare);
EXPORT_SYMBOL_GPL(mntns_operations);
EXPORT_SYMBOL_GPL(ns_get_path);
EXPORT_SYMBOL_GPL(path_mount);
EXPORT_SYMBOL_GPL(path_umount);
EXPORT_SYMBOL_GPL(policydb_destroy);
EXPORT_SYMBOL_GPL(policydb_filenametr_search);
EXPORT_SYMBOL_GPL(policydb_load_isids);
EXPORT_SYMBOL_GPL(policydb_read);
EXPORT_SYMBOL_GPL(policydb_write);
EXPORT_SYMBOL_GPL(seccomp_filter_release);
EXPORT_SYMBOL_GPL(set_fs_pwd);
EXPORT_SYMBOL_GPL(security_compute_av_user);
EXPORT_SYMBOL_GPL(security_context_str_to_sid);
EXPORT_SYMBOL_GPL(security_context_to_sid);
EXPORT_SYMBOL_GPL(security_sid_to_context);
EXPORT_SYMBOL_GPL(selinux_blob_sizes);
EXPORT_SYMBOL_GPL(selinux_state);
EXPORT_SYMBOL_GPL(selinux_status_update_policyload);
EXPORT_SYMBOL_GPL(selnl_notify_policyload);
EXPORT_SYMBOL_GPL(set_cred_ucounts);
EXPORT_SYMBOL_GPL(__set_fixmap);
EXPORT_SYMBOL_GPL(sidtab_destroy);
EXPORT_SYMBOL_GPL(strncpy_from_user_nofault);
EXPORT_SYMBOL_GPL(symtab_insert);
EXPORT_SYMBOL_GPL(symtab_search);
EXPORT_SYMBOL_GPL(task_work_add);
EXPORT_SYMBOL_GPL(__tracepoint_sys_enter);
