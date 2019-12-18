#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

static volatile int mymisc_var;

static int mymisc_open(struct inode *inode, struct file *file)
{
	printk("%s\n", __func__);
	mymisc_var++;

	return 0;
}

static int mymisc_release(struct inode *inode, struct file *file)
{
	printk("%s\n", __func__);
	return 0;
}

static ssize_t mymisc_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	printk("%s: %s\n", __func__, "enter");

#if 1
	while (1)
		if (mymisc_var & 1)
			;
		else
			break;
#endif

	printk("%s: %s\n", __func__, "exit");
	return 0;
}

static ssize_t mymisc_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	printk("%s\n", __func__);
	return 0;
}

static const struct file_operations mymisc_fops = {
	.read		= mymisc_read,
	.write		= mymisc_write,
	.open		= mymisc_open,
	.release	= mymisc_release,
};

static struct miscdevice mymisc_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "mymisc",
	.fops	= &mymisc_fops,
};

static int mymisc_init(void)
{
	int ret;

	printk("%s\n", __func__);

	ret = misc_register(&mymisc_dev);
	if (ret)
		return ret;

	printk("%s: %s\n", __func__, "success");

	return 0;
}

static void mymisc_exit(void)
{
	printk("%s\n", __func__);
	misc_deregister(&mymisc_dev);

	return;
}

module_init(mymisc_init)
module_exit(mymisc_exit)
MODULE_LICENSE("GPL");
