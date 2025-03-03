#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#ifndef EDEADLOCK
  #define EDEADLOCK EDEADLK
#endif
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include "memory.h"

#include "mhpdbg.h"
#include "emu.h"
#ifdef __linux__
#include "sys_vm86.h"
#endif
#include "bios.h"
#include "video.h"
#include "timers.h"
#include "cmos.h"
#include "mouse.h"
#include "xms.h"
#include "ipx.h"		/* TRB - add support for ipx */
#include "serial.h"
#include "int.h"
#include "bitops.h"
#include "pic.h"
#include "emudpmi.h"

#ifdef USE_MHPDBG
  #include "mhpdbg.h"
#endif


#ifndef PAGE_SIZE
#define PAGE_SIZE       4096
#endif

struct io_callback_s {
  void (*func)(int, void *);
  void *arg;
  const char *name;
};
#define MAX_FD 1024
static struct io_callback_s io_callback_func[MAX_FD];
static struct io_callback_s io_callback_stash[MAX_FD];
static fd_set fds_sigio;

#if defined(SIG)
static inline int process_interrupt(SillyG_t *sg)
{
  int irq, ret=0;

  if ((irq = sg->irq) != 0) {
    h_printf("INTERRUPT: 0x%02x\n", irq);
    pic_request(irq);
  }
  return ret;
}

static inline void irq_select(void)
{
  if (SillyG) {
    int irq_bits = vm86_plus(VM86_GET_IRQ_BITS, 0);
    if (irq_bits) {
      SillyG_t *sg=SillyG;
      while (sg->fd) {
        if (irq_bits & (1 << sg->irq)) {
          if (process_interrupt(sg)) {
            vm86_plus(VM86_GET_AND_RESET_IRQ,sg->irq);
            h_printf("SIG: We have an interrupt\n");
          }
        }
        sg++;
      }
    }
  }
}
#endif

/*  */
/* io_select @@@  24576 MOVED_CODE_BEGIN @@@ 01/23/96, ./src/base/misc/dosio.c --> src/base/misc/ioctl.c  */

static int numselectfd= 0;

void
io_select(void)
{
  int selrtn, i;
  struct timeval tvptr;
  fd_set fds = fds_sigio;

  tvptr.tv_sec=0L;
  tvptr.tv_usec=0L;

#if defined(SIG)
  irq_select();
#endif

  while ( ((selrtn = select(numselectfd, &fds, NULL, NULL, &tvptr)) == -1)
        && (errno == EINTR)) {
    tvptr.tv_sec=0L;
    tvptr.tv_usec=0L;
    g_printf("WARNING: interrupted io_select: %s\n", strerror(errno));
  }

  switch (selrtn) {
    case 0:			/* none ready, nothing to do :-) */
      return;

    case -1:			/* error (not EINTR) */
      error("bad io_select: %s\n", strerror(errno));
      break;

    default:			/* has at least 1 descriptor ready */
      for(i = 0; i < numselectfd; i++) {
        if (FD_ISSET(i, &fds) && io_callback_func[i].func) {
	  g_printf("GEN: fd %i has data for %s\n", i,
		io_callback_func[i].name);
	  io_callback_func[i].func(i, io_callback_func[i].arg);
	}
      }
      reset_idle(0);
      break;
    }
}

/*
 * DANG_BEGIN_FUNCTION add_to_io_select
 *
 * arguments:
 * fd - File handle to add to select statment
 * want_sigio - want SIGIO (1) if it's available, or not (0).
 *
 * description:
 * Add file handle to one of 2 select FDS_SET's depending on
 * whether the kernel can handle SIGIO.
 *
 * DANG_END_FUNCTION
 */
void
add_to_io_select_new(int new_fd, void (*func)(int, void *), void *arg,
	const char *name)
{
    int flags;

    if ((new_fd+1) > numselectfd) numselectfd = new_fd+1;
    if (numselectfd > MAX_FD) {
	error("Too many IO fds used.\n");
	leavedos(76);
    }

    io_callback_stash[new_fd] = io_callback_func[new_fd];

    if (!io_callback_func[new_fd].func) {
	if ((flags = fcntl(new_fd, F_GETFL)) == -1 ||
                fcntl(new_fd, F_SETOWN, getpid()) == -1 ||
                fcntl(new_fd, F_SETFL, flags | O_ASYNC) == -1 ||
                fcntl(new_fd, F_SETFD, FD_CLOEXEC) == -1) {
	    error("add_to_io_select_new: Fcntl failed\n");
	    leavedos(76);
	}
	FD_SET(new_fd, &fds_sigio);
    }

    g_printf("GEN: fd=%d gets SIGIO for %s\n", new_fd, name);
    io_callback_func[new_fd].func = func;
    io_callback_func[new_fd].arg = arg;
    io_callback_func[new_fd].name = name;
}

/*
 * DANG_BEGIN_FUNCTION remove_from_io_select
 *
 * arguments:
 * fd - File handle to remove from select statment.
 * used_sigio - used SIGIO (1) if it's available, or not (0).
 *
 * description:
 * Remove a file handle from one of 2 select FDS_SET's depending
 * on whether the kernel can handle SIGIO.
 *
 * DANG_END_FUNCTION
 */
void remove_from_io_select(int fd)
{
    int flags;

    if (fd < 0) {
	g_printf("GEN: removing bogus fd %d (ignoring)\n", fd);
	return;
    }

    io_callback_func[fd] = io_callback_stash[fd];
    io_callback_stash[fd].func = NULL;

    if (!io_callback_func[fd].func) {
	if ((flags = fcntl(fd, F_GETFL)) == -1 ||
                fcntl(fd, F_SETOWN, NULL) == -1 ||
                fcntl(fd, F_SETFL, flags & ~O_ASYNC) == -1) {
	    error("remove_from_io_select: Fcntl failed\n");
	    return;
	}
	FD_CLR(fd, &fds_sigio);
	g_printf("GEN: fd=%d removed from select SIGIO\n", fd);
    }
}

void ioselect_done(void)
{
    int i;
    for (i = 0; i < MAX_FD; i++) {
	if (io_callback_func[i].func) {
	    remove_from_io_select(i);
	    close(i);
	}
    }
}
