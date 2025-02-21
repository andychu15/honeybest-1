#include <linux/init.h>
#include <linux/kd.h>
#include <linux/kernel.h>
#include <linux/tracehook.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/lsm_hooks.h>
#include <linux/xattr.h>
#include <linux/capability.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/tty.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <net/net_namespace.h>
#include <net/netlabel.h>
#include <linux/uaccess.h>
#include <asm/ioctls.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>	/* for network interface checks */
#include <net/netlink.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/dccp.h>
#include <linux/quota.h>
#include <linux/un.h>		/* for Unix socket types */
#include <net/af_unix.h>	/* for Unix socket types */
#include <linux/parser.h>
#include <linux/nfs_mount.h>
#include <net/ipv6.h>
#include <linux/hugetlb.h>
#include <linux/personality.h>
#include <linux/audit.h>
#include <linux/lsm_audit.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/posix-timers.h>
#include <linux/syslog.h>
#include <linux/user_namespace.h>
#include <linux/export.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <net/xfrm.h>
#include <linux/xfrm.h>
#include <linux/binfmts.h>
#include <linux/cred.h>
#include <linux/path.h>
#include <linux/string_helpers.h>
#include <linux/list.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/algapi.h>
#include "inode.h"
#include "regex.h"
#include "notify.h"
#include "honeybest.h"

struct proc_dir_entry *hb_proc_inode_entry;
hb_inode_ll hb_inode_list_head;
hb_inode_ll *search_inode_record(unsigned int fid, uid_t uid, char *name, char *dname, umode_t mode)
{
	hb_inode_ll *tmp = NULL;
	struct list_head *pos = NULL;

	list_for_each(pos, &hb_inode_list_head.list) {
		tmp = list_entry(pos, hb_inode_ll, list);
		switch (fid) {
			case HB_INODE_REMOVEXATTR:
			case HB_INODE_GETXATTR:
			case HB_INODE_SETXATTR:
				if ((fid == tmp->fid) && (uid == tmp->uid) && (tmp->mode == mode) && !compare_regex(tmp->name, name, strlen(name)) && !compare_regex(tmp->dname, dname, strlen(dname))) {
					/* we find the record */
					printk(KERN_INFO "Found inode open record !!!!\n");
					return tmp;
				}
				break;
			default:
				break;
		}
	}
	return NULL;
}

int add_inode_record(unsigned int fid, uid_t uid, char *name, char *dname, umode_t mode, int interact)
{
	int err = 0;
	hb_inode_ll *tmp = NULL;

	tmp = (hb_inode_ll *)kmalloc(sizeof(hb_inode_ll), GFP_KERNEL);
	if (tmp) {
		tmp->fid = fid;
		tmp->uid = uid;
		tmp->name = kmalloc(strlen(name)+1, GFP_KERNEL);
	       	tmp->dname = kmalloc(strlen(dname)+1, GFP_KERNEL);
		if (tmp->name == NULL) {
			err = -EOPNOTSUPP;
			goto out;
		}
		if (tmp->dname == NULL) {
			kfree(tmp->name);
			err = -EOPNOTSUPP;
			goto out;
		}

		switch (fid) {
			case HB_INODE_REMOVEXATTR:
			case HB_INODE_GETXATTR:
			case HB_INODE_SETXATTR:
				strcpy(tmp->name, name);
				strcpy(tmp->dname, dname);
				break;
			default:
				break;
		}

		if ((err == 0) && (interact == 0))
		       	list_add(&(tmp->list), &(hb_inode_list_head.list));

		if ((err == 0) && (interact == 1))
			add_notify_record(fid, tmp);
	}
	else
		err = -EOPNOTSUPP;

out:
	return err;
}

int read_inode_record(struct seq_file *m, void *v)
{
	hb_inode_ll *tmp = NULL;
	struct list_head *pos = NULL;
	unsigned long total = 0;

	seq_printf(m, "NO\tFUNC\tUID\tMODE\tNAME\t\t\tDENTRY NAME\n");

	if (list_empty(&hb_inode_list_head.list)) {
		printk(KERN_WARNING "List is empty!!\n");
		return 0;
	}

	list_for_each(pos, &hb_inode_list_head.list) {
		tmp = list_entry(pos, hb_inode_ll, list);
		seq_printf(m, "%lu\t%u\t%u\t%u\t%s\t%s\n", total++, tmp->fid, tmp->uid, tmp->mode, tmp->name, tmp->dname);
	}

	return 0;
}

ssize_t write_inode_record(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	char *acts_buff = NULL;
	char *delim = "\n";
	char *token, *cur;
	hb_inode_ll *tmp = NULL;
	struct list_head *pos = NULL;
	struct list_head *q = NULL;

	if(*ppos > 0 || count > TOTAL_ACT_SIZE) {
		printk(KERN_WARNING "Write size is too big!\n");
		count = 0;
		goto out;
	}

	acts_buff = (char *)kmalloc(TOTAL_ACT_SIZE, GFP_KERNEL);
	if (acts_buff == NULL) {
		count = 0;
		goto out1;
	}
	memset(acts_buff, '\0', TOTAL_ACT_SIZE);

	if (count <= 0) {
		goto out1;
	}

	if(copy_from_user(acts_buff, buffer, count))
		goto out1;

	/* clean all acts_buff */
	list_for_each_safe(pos, q, &hb_inode_list_head.list) {
		tmp = list_entry(pos, hb_inode_ll, list);
		list_del(pos);
		kfree(tmp->name);
		kfree(tmp->dname);
		kfree(tmp);
		tmp = NULL;
	}

       	cur = acts_buff;
	/* add acts_buff */
	while((token = strsep(&cur, delim)) && (strlen(token)>1)) {
		uid_t uid = 0;
		unsigned int fid = 0;
		umode_t mode = 0;
		char *filename = NULL;
		char *dirname = NULL;

		filename = (char *)kmalloc(PATH_MAX, GFP_KERNEL);
		if (filename == NULL) {
			continue;
		}

		dirname = (char *)kmalloc(PATH_MAX, GFP_KERNEL);
		if (dirname == NULL) {
			kfree(filename);
			continue;
		}

		sscanf(token, "%u %u %hd %s %s", &fid, &uid, &mode, filename, dirname);
		if (add_inode_record(fid, uid, filename, dirname, mode, 0) != 0) {
			printk(KERN_WARNING "Failure to add inode record %u, %s, %s\n", uid, filename, dirname);
		}
		kfree(filename);
		kfree(dirname);
	} //while
out1:
	kfree(acts_buff);
out:
	return count;
}
