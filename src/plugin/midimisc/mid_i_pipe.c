/*
 *  Copyright (C) 2006 Stas Sergeev <stsp@users.sourceforge.net>
 *
 * The below copyright strings have to be distributed unchanged together
 * with this file. This prefix can not be modified or separated.
 */

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include "emu.h"
#include "dosemu_config.h"
#include "init.h"
#include "sound/midi.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>


static int pipe_fd = -1;
#define midipipe_name "MIDI Input: named pipe"

static void midipipe_io(int fd, void *arg)
{
    unsigned char buf[1024];
    int n, selret;
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(pipe_fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    while ((selret = select(pipe_fd + 1, &rfds, NULL, NULL, &tv)) > 0) {
	n = RPT_SYSCALL(read(pipe_fd, buf, sizeof(buf)));
	if (n > 0) {
	    midi_put_data(buf, n);
	} else {
	    break;
	}
	FD_ZERO(&rfds);
	FD_SET(pipe_fd, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
    }
}

static int midipipe_init(void *arg)
{
    const char *name = DOSEMU_MIDI_IN_PATH;
    pipe_fd = RPT_SYSCALL(open(name, O_RDONLY | O_NONBLOCK));
    if (pipe_fd == -1) {
	S_printf("%s: unable to open %s for reading: %s\n",
		 midipipe_name, name, strerror(errno));
	return 0;
    }
    add_to_io_select(pipe_fd, midipipe_io, NULL);
    return 1;
}

static void midipipe_done(void *arg)
{
    if (pipe_fd == -1)
	return;
    remove_from_io_select(pipe_fd);
    close(pipe_fd);
    pipe_fd = -1;
}

static const struct midi_in_plugin midipipe
#ifdef __cplusplus
{
    midipipe_name,
    midipipe_init,
    midipipe_done,
};
#else
= {
    .name = midipipe_name,
    .open = midipipe_init,
    .close = midipipe_done,
};
#endif

CONSTRUCTOR(static void midipipe_register(void))
{
    midi_register_input_plugin(&midipipe);
}
