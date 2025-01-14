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

/*
 * Purpose: glue to qemu PIC impl.
 *
 * Author: stsp
 *
 */
#include "port.h"
#include "dosemu_debug.h"
#include "i8259.h"
#include "i8259_internal.h"
#include "pic.h"

static PICCommonState pic[2];
PICCommonState *slave_pic;

static void write_pic0(ioport_t port, Bit8u value)
{
    r_printf("PIC0: write 0x%x --> 0x%x\n", value, port);
    pic_ioport_write(&pic[0], port - 0x20, value, 1);
    r_printf("PIC0: isr=%x imr=%x irr=%x\n",
            pic[0].isr, pic[0].imr, pic[0].irr);
}

static void write_pic1(ioport_t port, Bit8u value)
{
    r_printf("PIC1: write 0x%x --> 0x%x\n", value, port);
    pic_ioport_write(&pic[1], port - 0xa0, value, 1);
    r_printf("PIC1: isr=%x imr=%x irr=%x\n",
            pic[1].isr, pic[1].imr, pic[1].irr);
}

static Bit8u read_pic0(ioport_t port)
{
    Bit8u ret = pic_ioport_read(&pic[0], port - 0x20, 1);
    r_printf("PIC0: read 0x%x <-- 0x%x\n", ret, port);
    return ret;
}

static Bit8u read_pic1(ioport_t port)
{
    Bit8u ret = pic_ioport_read(&pic[1], port - 0xa0, 1);
    r_printf("PIC1: read 0x%x <-- 0x%x\n", ret, port);
    return ret;
}

static Bit8u read_elcr(ioport_t port)
{
    return elcr_ioport_read(&pic[port & 1], port & 1, 1);
}

static void write_elcr(ioport_t port, Bit8u value)
{
    elcr_ioport_write(&pic[port & 1], port & 1, value, 1);
}

void pic_request(int irq)
{
    PICCommonState *p = pic;

    r_printf("PIC: Requested irq lvl %x\n", irq);
    if (irq >= 8) {
        irq -= 8;
        p++;
    }
    pic_set_irq(p, irq, 1);
    r_printf("PIC%i: isr=%x imr=%x irr=%x\n",
            p->master ? 0 : 1, p->isr, p->imr, p->irr);
}

void pic_untrigger(int irq)
{
    PICCommonState *p = pic;

    r_printf("PIC: irq lvl %x untriggered\n", irq);
    if (irq >= 8) {
        irq -= 8;
        p++;
    }
    pic_set_irq(p, irq, 0);
    r_printf("PIC%i: isr=%x imr=%x irr=%x\n",
            p->master ? 0 : 1, p->isr, p->imr, p->irr);
}

int pic_get_inum(void)
{
    int inum;

    if (!slave_pic)
        slave_pic = &pic[1];
    inum = pic_read_irq(&pic[0]);
    r_printf("PIC: Running interrupt %x\n", inum);
    return inum;
}

static void set_irq_level(void *opaque, int n, int level)
{
    r_printf("PIC: Cascade irq %i set to %i\n", n, level);
    pic_set_irq(opaque, n, level);
}

void pic_init(void)
{
    /* do any one-time initialization of the PIC */
    emu_iodev_t  io_device;

    /* 8259 PIC (Programmable Interrupt Controller) */
    io_device.read_portb   = read_pic0;
    io_device.write_portb  = write_pic0;
    io_device.read_portw   = NULL;
    io_device.write_portw  = NULL;
    io_device.read_portd   = NULL;
    io_device.write_portd  = NULL;
    io_device.handler_name = "8259 PIC0";
    io_device.start_addr   = 0x0020;
    io_device.end_addr     = 0x0021;
    port_register_handler(io_device, 0);

    io_device.handler_name = "8259 PIC1";
    io_device.start_addr = 0x00A0;
    io_device.end_addr   = 0x00A1;
    io_device.read_portb   = read_pic1;
    io_device.write_portb  = write_pic1;
    port_register_handler(io_device, 0);

    io_device.handler_name = "ELCR";
    io_device.start_addr = 0x04D0;
    io_device.end_addr   = 0x04D1;
    io_device.read_portb   = read_elcr;
    io_device.write_portb  = write_elcr;
    port_register_handler(io_device, 0);

    /* set up cascading bits */
    pic[0].master = 1;
    pic[1].int_out[0] = &pic[1]._int_out;
    pic[1]._int_out.handler = set_irq_level;
    pic[1]._int_out.opaque = &pic[0];
    pic[1]._int_out.n = 2;
    /* set up qemu extensions */
    pic[0].elcr_mask = 0xf8;
    pic[1].elcr_mask = 0xde;
}

void pic_reset(void)
{
    qemu_pic_reset(&pic[0]);
    qemu_pic_reset(&pic[1]);
}

/* PIC extensions */

Bit8u pic0_get_base(void)
{
    return pic[0].irq_base;
}

Bit8u pic1_get_base(void)
{
    return pic[1].irq_base;
}

unsigned pic_get_isr(void)
{
    return (pic[0].isr | (pic[1].isr << 8));
}

int pic_pending(void)
{
    return pic_get_output(&pic[0]);
}

int pic_irq_active(int irq)
{
    PICCommonState *p = pic;
    if (irq >= 8) {
        irq -= 8;
        p++;
    }
    return !!((1 << irq) & p->isr);
}
