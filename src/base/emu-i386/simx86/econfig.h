/***************************************************************************
 *
 * All modifications in this file to the original code are
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 *
 *
 *  SIMX86 a Intel 80x86 cpu emulator
 *  Copyright (C) 1997,2001 Alberto Vignani, FIAT Research Center
 *				a.vignani@crf.it
 *
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Additional copyright notes:
 *
 * 1. The kernel-level vm86 handling was taken out of the Linux kernel
 *  (linux/arch/i386/kernel/vm86.c). This code originaly was written by
 *  Linus Torvalds with later enhancements by Lutz Molgedey and Hans Lermen.
 *
 ***************************************************************************/

#ifndef _EMU86_CONFIG_H
#define _EMU86_CONFIG_H

/***************************************************************************/

#undef	SINGLESTEP
#undef	SINGLEBLOCK
#undef	PROFILE
#undef	DBG_TIME
#undef	SKIP_CPATCH

#define	FAKE_INS_TIME	20

#define MAXINODES	4096
#define MAX_GEND_BYTES_PER_OP 70
/* NUMGENS must be large enough in !SINGLESTEP mode */
#define NUMGENS		128
#undef	ASM_DUMP
#define ASM_DUMP_FILE	"/DOS/asmdump.log"

#define AVL_MAX_HEIGHT	20
#undef	DEBUG_TREE
#define DEBUG_TREE_FILE	"/DOS/treedump.log"

#define	USE_LINKER	1	// 0 or 1
#undef	DEBUG_LINKER
#undef	SHOW_STAT

#define	FP_DISPHEX
#undef	FPU_TAGS

#undef	DEBUG_VGA

#define NODES_IN_POOL	100000
#define NODELIFE(n)	200
#define CLEAN_SPEED(n)	(((n)<<2)+1)
#define AGENODE		CreationIndex

#undef	TRAP_RETRACE

/* If you undefine this, in 16-bit stack mode the high 16 bits of ESP
 * will be zeroed after every push/pop operation. There's a small
 * possibility of breaking some code, you can easily figure out how.
 * For 32-bit stacks, keeping ESP is also a waste of time. */
#define KEEP_ESP 1

/////////////////////////////////////////////////////////////////////////////

#endif
