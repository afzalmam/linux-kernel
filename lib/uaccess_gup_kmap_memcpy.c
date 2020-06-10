// SPDX-License-Identifier: GPL-2.0
// Started from arch/um/kernel/skas/uaccess.c

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>

static int do_op_one_page(unsigned long addr, int len,
		 int (*op)(unsigned long addr, int len, void *arg), void *arg,
		 struct page *page)
{
	int n;

	addr = (unsigned long) kmap_atomic(page) + (addr & ~PAGE_MASK);
	n = (*op)(addr, len, arg);
	kunmap_atomic((void *)addr);

	return n;
}

static long buffer_op(unsigned long addr, int len,
		      int (*op)(unsigned long, int, void *), void *arg,
		      struct page **pages)
{
	long size, remain, n;

	size = min(PAGE_ALIGN(addr) - addr, (unsigned long) len);
	remain = len;
	if (size == 0)
		goto page_boundary;

	n = do_op_one_page(addr, size, op, arg, *pages);
	if (n != 0) {
		remain = (n < 0 ? remain : 0);
		goto out;
	}

	pages++;
	addr += size;
	remain -= size;

page_boundary:
	if (remain == 0)
		goto out;
	while (addr < ((addr + remain) & PAGE_MASK)) {
		n = do_op_one_page(addr, PAGE_SIZE, op, arg, *pages);
		if (n != 0) {
			remain = (n < 0 ? remain : 0);
			goto out;
		}

		pages++;
		addr += PAGE_SIZE;
		remain -= PAGE_SIZE;
	}
	if (remain == 0)
		goto out;

	n = do_op_one_page(addr, remain, op, arg, *pages);
	if (n != 0) {
		remain = (n < 0 ? remain : 0);
		goto out;
	}

	return 0;
out:
	return remain;
}

static int copy_chunk_from_user(unsigned long from, int len, void *arg)
{
	unsigned long *to_ptr = arg, to = *to_ptr;

	memcpy((void *) to, (void *) from, len);
	*to_ptr += len;
	return 0;
}

static int copy_chunk_to_user(unsigned long to, int len, void *arg)
{
	unsigned long *from_ptr = arg, from = *from_ptr;

	memcpy((void *) to, (void *) from, len);
	*from_ptr += len;
	return 0;
}

unsigned long gup_kmap_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	struct page **pages;
	int num_pages, ret, i;

	if (uaccess_kernel()) {
		memcpy(to, (__force void *)from, n);
		return 0;
	}

	num_pages = DIV_ROUND_UP((unsigned long)from + n, PAGE_SIZE) -
				 (unsigned long)from / PAGE_SIZE;
	pages = kmalloc_array(num_pages, sizeof(*pages), GFP_KERNEL | __GFP_ZERO);
	if (!pages)
		goto end;

	ret = get_user_pages_fast((unsigned long)from, num_pages, 0, pages);
	if (ret < 0)
		goto free_pages;

	if (ret != num_pages) {
		num_pages = ret;
		goto put_pages;
	}

	n = buffer_op((unsigned long) from, n, copy_chunk_from_user, &to, pages);

put_pages:
	for (i = 0; i < num_pages; i++)
		put_page(pages[i]);
free_pages:
	kfree(pages);
end:
	return n;
}

unsigned long gup_kmap_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	struct page **pages;
	int num_pages, ret, i;

	if (uaccess_kernel()) {
		memcpy((__force void *) to, from, n);
		return 0;
	}

	num_pages = DIV_ROUND_UP((unsigned long)to + n, PAGE_SIZE) - (unsigned long)to / PAGE_SIZE;
	pages = kmalloc_array(num_pages, sizeof(*pages), GFP_KERNEL | __GFP_ZERO);
	if (!pages)
		goto end;

	ret = get_user_pages_fast((unsigned long)to, num_pages, FOLL_WRITE, pages);
	if (ret < 0)
		goto free_pages;

	if (ret != num_pages) {
		num_pages = ret;
		goto put_pages;
	}


	n = buffer_op((unsigned long) to, n, copy_chunk_to_user, &from, pages);

put_pages:
	for (i = 0; i < num_pages; i++)
		put_page(pages[i]);
free_pages:
	kfree(pages);
end:
	return n;
}
