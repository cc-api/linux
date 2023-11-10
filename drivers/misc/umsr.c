#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>

#define UMSR_VERSION		"0.0.1"
#define UMSR_NAME 		"umsr"

#define UMSR_ALLOW_ENABLE	0x1
#define UMSR_ALLOW_READ		0x2
#define UMSR_ALLOW_WRITE	0x4

#define UMSR_WRITE_OFFSET	0x800

struct umsr_req {
	u32 msr;
	u32 allow;
};

struct miscdevice umsr_dev;

static int umsr_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int umsr_release(struct inode *inode, struct file *file)
{
	struct thread_struct *c = &current->thread;

	c->umsr_control = 0;
	__free_pages(virt_to_page(c->umsr_bitmap), 0);
	c->umsr_bitmap = NULL;
	wrmsrl(MSR_IA32_USER_MSR_CTL, c->umsr_control);

	return 0;
}

static ssize_t umsr_read(struct file *file, char __user *buffer,
			      size_t count, loff_t *ppos)
{
	/* FIXME: return the msrs that has been allowed */
	return 0;
}

static int enable_umsr(struct umsr_req *req)
{
	u64 msr = req->msr;
	struct thread_struct *c = &current->thread;

	if (!c->umsr_bitmap) {
		struct page *pages = alloc_pages(GFP_KERNEL, 0);
		if (!pages)
			return -ENOMEM;
		c->umsr_bitmap = page_address(pages);
		memset(c->umsr_bitmap, 0x0, PAGE_SIZE * (1 << 0));
	}

	if (req->allow & UMSR_ALLOW_READ)
		c->umsr_bitmap[msr >> 3] |= 1UL << (msr & 0x3);
	if (req->allow & UMSR_ALLOW_WRITE)
		c->umsr_bitmap[UMSR_WRITE_OFFSET + (msr >> 3)] |= 1UL << (msr & 0x3);
	c->umsr_control = ((u64)c->umsr_bitmap & USER_MSR_CTL_BITMAPADDR) | USER_MSR_CTL_ENABLE;
	wrmsrl(MSR_IA32_USER_MSR_CTL, c->umsr_control);
	return 0;
}

static int disable_umsr(struct umsr_req *req)
{
	u64 msr = req->msr;
	struct thread_struct *c = &current->thread;

	if(!(c->umsr_control & USER_MSR_CTL_ENABLE))
		return 0;
	if (req->allow & UMSR_ALLOW_READ)
		c->umsr_bitmap[msr >> 3] &= ~(1UL << (msr & 0x3));
	if (req->allow & UMSR_ALLOW_WRITE)
		c->umsr_bitmap[UMSR_WRITE_OFFSET + (msr >> 3)] &= ~(1UL << (msr & 0x3));

	return 0;
}

static inline bool valid_umsr(struct umsr_req *req)
{
	return req->msr & ~0x3fff ? false: true;
}

static ssize_t umsr_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *ppos)
{
	struct umsr_req r;
	int ret;

	if (count < sizeof(struct umsr_req))
		return -EFAULT;

	if(copy_from_user(&r, (void __user *)buffer, sizeof(struct umsr_req)))
		return -EFAULT;

	if (!valid_umsr(&r))
		return -EINVAL;

	if (r.allow & UMSR_ALLOW_ENABLE)
		ret = enable_umsr(&r);
	else
		ret = disable_umsr(&r);
	return ret ? ret :sizeof(struct umsr_req);
}

static int umsr_fasync(int fd, struct file *file, int on)
{
	return 0;
}

static __poll_t umsr_poll(struct file *file, poll_table *wait)
{
	return 0;
}

static const struct file_operations umsr_fops = {
	.owner		= THIS_MODULE,
	.open		= umsr_open,
	.release	= umsr_release,
	.read		= umsr_read,
	.write		= umsr_write,
	.poll		= umsr_poll,
	.fasync		= umsr_fasync,
	.llseek		= noop_llseek,
};

static int __init umsr_init(void)
{
	int err;

	if (!cpu_feature_enabled(X86_FEATURE_USER_MSR))
		return -ENOSYS;
	umsr_dev.minor = MISC_DYNAMIC_MINOR;
	umsr_dev.name = UMSR_NAME;
	umsr_dev.fops = &umsr_fops;
	/* allow all permission as a test module */
	umsr_dev.mode = S_IRWXUGO;

	err = misc_register(&umsr_dev);
	if (err) {
		printk(KERN_ERR "failed to register the umsr devices\n");
		return err;
	}
	return 0;
}

static void __exit umsr_exit(void)
{
	misc_deregister(&umsr_dev);
}

module_init(umsr_init);
module_exit(umsr_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yunhong Jiang");
MODULE_DESCRIPTION("umsr test kernel module");
MODULE_VERSION(UMSR_VERSION);
