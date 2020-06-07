// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

// #define DEBUG1
#define DEBUG2
#ifdef DEBUG1
#define	pr_my1(fmt, ...)	printk(pr_fmt(fmt), ##__VA_ARGS__)
#else
#define	pr_my1(fmt, ...)
#endif
#ifdef DEBUG2
#define	pr_my2(fmt, ...)	printk(pr_fmt(fmt), ##__VA_ARGS__)
#else
#define	pr_my2(fmt, ...)
#endif

static pgd_t *pgd_zero;

static pte_t *virt_to_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	if (mm == NULL)
		return NULL;

	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (!p4d_present(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (!pud_present(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		return NULL;

	return pte_offset_map(pmd, addr);
}

static struct page *get_original_page(unsigned long addr, pte_t **ppte)
{
	struct page *page;
	pte_t *pte;

	pte = virt_to_pte(current->mm, addr);
	page = pte_page(*pte);
	*ppte = pte;
	return page;
}

static int do_op_one_page(unsigned long addr, int len,
		 int (*op)(unsigned long addr, int len, void *arg), void *arg,
		 struct page *page)
{
	int n;

	addr = (unsigned long) kmap_atomic(page) +
		(addr & ~PAGE_MASK);
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

 	pr_my1("[%s] addr: %lx, size: %ld, page: %px\n", __func__, addr, size, *pages);

	n = do_op_one_page(addr, size, op, arg, *pages);
	if (n != 0) {
		remain = (n < 0 ? remain : 0);
		goto out;
	}

	if (size)
		pages++;
	addr += size;
	remain -= size;
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

unsigned long arm_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	struct page **pages, *orig_page;
	int num_pages, ret, i;
	pte_t *pte, pte_val;

	pr_my1("[%s] to: %px, from: %px, n: %lu\n", __func__, to, from, n);
	if (uaccess_kernel()) {
		pr_my1("[%s] uaccess_kernel() true\n", __func__);
		memcpy(to, (__force void*)from, n);
		return 0;
	}
	pr_my1("[%s] user space copy\n", __func__);

	orig_page = get_original_page((unsigned long)to, &pte);
	pte_val = *pte;
	pte_unmap(pte);
	pr_my1("[%s] user page: %px, pte: %llx\n", __func__, orig_page, pte_val);

	num_pages = DIV_ROUND_UP((unsigned long)from + n, PAGE_SIZE) - (unsigned long)from / PAGE_SIZE;
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

	pr_my1("[%s] num_pages: %d, page: %px\n", __func__, num_pages, *pages);

 	n = buffer_op((unsigned long) from, n, copy_chunk_from_user, &to, pages);

put_pages:
	for (i = 0; i < num_pages; i++)
		put_page(pages[i]);
free_pages:
	kfree(pages);
end:
 
	return n;
}
EXPORT_SYMBOL(arm_copy_from_user);

unsigned long arm_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	struct page **pages, *orig_page;
	int num_pages, ret, i;
	pte_t *pte, pte_val;
	u32 ttbr0l_orig, ttbr0h_orig, ttbr0l_new, ttbr0h_new;

	pr_my1("[%s] to: %px, from: %px, n: %lu\n", __func__, to, from, n);
	if (uaccess_kernel()) {
		pr_my1("[%s] uaccess_kernel() true\n", __func__);
		memcpy((__force void *) to, from, n);
		return 0;
	}
	pr_my1("[%s] user space copy\n", __func__);

	ttbr0h_new = ttbr0l_new = 0;
	asm volatile (	"mrrc p15, 0, %0, %1, c2\n"
			"mcrr p15, 0, %2, %3, c2\n"
			"isb"
			: "=r" (ttbr0l_orig), "=r" (ttbr0h_orig)
			: "r" (ttbr0l_new), "r" (ttbr0h_new)
			:);

	pr_my1("[%s] ttbr0h: %x, ttbr0l: %x\n", __func__, ttbr0h_orig, ttbr0l_orig);

	orig_page = get_original_page((unsigned long)to, &pte);
	pte_val = *pte;
	pte_unmap(pte);
	pr_my1("[%s] user page: %px, pte: %llx\n", __func__, orig_page, pte_val);

	num_pages = DIV_ROUND_UP((unsigned long)to + n, PAGE_SIZE) - (unsigned long)to / PAGE_SIZE;
	pages = kmalloc_array(num_pages, sizeof(*pages), GFP_KERNEL | __GFP_ZERO);
	if (!pages)
		goto end;

	ret = get_user_pages_fast((unsigned long)to, num_pages, 0, pages);
	if (ret < 0)
		goto free_pages;

	if (ret != num_pages) {
		num_pages = ret;
		goto put_pages;
	}

	pr_my1("[%s] num_pages: %d, page: %px\n", __func__, num_pages, *pages);

 	n = buffer_op((unsigned long) to, n, copy_chunk_to_user, &from, pages);

put_pages:
	for (i = 0; i < num_pages; i++)
		put_page(pages[i]);
free_pages:
	kfree(pages);
end:
 
	asm volatile (	"mcrr p15, 0, %0, %1, c2\n"
			"isb"
			:
			: "r" (ttbr0l_orig), "r" (ttbr0h_orig)
			:);

	pr_my1("[%s] ttbr0h: %x, ttbr0l: %x\n", __func__, ttbr0h_orig, ttbr0l_orig);

	return n;
}
EXPORT_SYMBOL(arm_copy_to_user);

static int __init create_no_user_pgd(void)
{
	pgd_zero = pgd_alloc(&init_mm);
	if (!pgd_zero)
		printk("[%s] pgd_alloc failed\n", __func__);
	else
		printk("[%s] pgd_zero: %px\n", __func__, pgd_zero);

	return 0;
}
core_initcall(create_no_user_pgd);
