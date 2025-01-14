/*
 * DANG_BEGIN_MODULE
 *
 * Description: Timer emulation for DOSEMU.
 *
 * Maintainers: J. Lawrence Stephan
 *              Scott Buchholz
 *
 * REMARK
 * This is the timer emulation for DOSEMU.  It emulates the Programmable
 * Interval Timer (PIT), and also handles IRQ0 interrupt events.
 * A lot of animation and video game software are dependant on this module
 * for high frequency timer interrupts (IRQ0).
 *
 * This code will actually generate 18.2 DOS interrupts/second (the code
 * here itself will be triggered about 100 times per second). It will even
 * happily attempt to generate faster clocks, right up to the point where
 * it chokes.  Since the absolute best case timing we can get out of Linux
 * is 100Hz, figure that anything approaching or exceeding that isn't going
 * to work well.  (The code will attempt to generate up to 10Khz interrupts
 * per second at the moment.  Too bad that would probably overflow all
 * internal queues really fast. :)
 *
 * Speaker emulation, now including port 61h, is also in here. [rz]
 *
 * /REMARK
 * DANG_END_MODULE
 *
 */

#include <sys/time.h>
#include <sys/ioctl.h>

#include "emu.h"
#include "port.h"
#include "iodev.h"
#include "int.h"
#include "emudpmi.h"
#include "vtmr.h"
#include "timers.h"

#undef  DEBUG_PIT
#undef  ONE_MINUTE_TEST

pit_latch_struct pit[PIT_TIMERS];   /* values of 3 PIT counters */

static u_long timer_div;          /* used by timer int code */
static u_long ticks_accum;        /* For timer_tick function, 100usec ticks */
static hitimer_t pic_dos_time;     /* dos time of last interrupt,1193047/sec.*/
hitimer_t pic_sys_time;     /* system time set by pic_watch */

#define NEVER -1
static hitimer_t pic_ltime[33] =     /* timeof last pic request honored */
                {NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER,
                 NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER,
                 NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER,
                 NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER,
                 NEVER};
static hitimer_t pic_itime[33] =     /* time to trigger next interrupt */
                {NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER,
                 NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER,
                 NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER,
                 NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER, NEVER,
                 NEVER};

static Bit8u port61 = 0x0c;

int is_cli;

static void pic_watch(hitimer_u *s_time);

/*
 * DANG_BEGIN_FUNCTION initialize_timers
 *
 * description:
 * ensure the 0x40 port timer is initially set correctly
 *
 * DANG_END_FUNCTION
 */
void initialize_timers(void)
{
  hitimer_t cur_time;

  cur_time = GETtickTIME(0);

  pit[0].mode        = 3;
  pit[0].outpin      = 0;
  pit[0].cntr        = 0x10000;
  pit[0].time.td     = cur_time;
  pit[0].read_latch  = -1;
  pit[0].write_latch = 0;
  pit[0].read_state  = 3;
  pit[0].write_state = 3;

  pit[1].mode        = 2;
  pit[1].outpin      = 0;
  pit[1].cntr        = 18;
  pit[1].time.td     = cur_time;
  pit[1].read_latch  = -1;
  pit[1].write_latch = 18;
  pit[1].read_state  = 3;
  pit[1].write_state = 3;

  pit[2].mode        = 0;
  pit[2].outpin      = 0;
  pit[2].cntr        = -1;
  pit[2].time.td     = cur_time;
  pit[2].read_latch  = -1;
  pit[2].write_latch = 0;
  pit[2].read_state  = 3;
  pit[2].write_state = 3;

  ticks_accum   = 0;
  timer_div     = (pit[0].cntr * 10000) / PIT_TICK_RATE;

  timer_tick();  /* a starting tick! */

  port61 = 0x0c;
}

/*
 * DANG_BEGIN_FUNCTION timer_tick
 *
 * description:
 *  Every time we get a TIMER signal from Linux, this procedure is called.
 *  It checks to see if we should queue a timer interrupt based on the
 *  current values.
 *
 * DANG_END_FUNCTION
 */
void timer_tick(void)
{
#ifdef ONE_MINUTE_TEST
  static int dbug_count = 0;
#endif
  hitimer_u tp;
  u_long time_curr;
  static u_long time_old = 0;       /* Preserve value for next call */

  /* Get system time in ticks */
  tp.td = GETtickTIME(0);
#ifdef ONE_MINUTE_TEST
  if (++dbug_count > 6000) leavedos(0);
#endif

  /* compute the number of 100ticks since we started */
  time_curr  = (tp.td - pit[0].time.td) / 100;

  /* Reset old timer value to 0 if time_curr wrapped around back to 0 */
  if (time_curr < time_old) time_old = 0;

  /* Compute number of 100ticks ticks since the last time this function ran */
  ticks_accum += (time_curr - time_old);

  /* Save old value of the timer */
  time_old = time_curr;

  if (config.cli_timeout && is_cli) {
    if (isset_IF()) {
      is_cli = 0;
    } else if (is_cli++ >= config.cli_timeout) {
      g_printf("Warning: Interrupts were disabled for too long, "
      "re-enabling.\n");
      set_IF();
    }
  }
  dpmi_timer();

  /* test for stuck interrupts, trigger any scheduled interrupts */
  pic_watch(&tp);
}


#include "speaker.h"

/* DANG_BEGIN_FUNCTION do_sound
 *
 * do_sound handles the _emulated_ mode pc-speaker emulation.
 *
 * As far as I can determine all cases of the pc-speaker are now
 * emulated.  But I am not sure where Rainer Zimmerman got his
 * (pit[2].mode == 2) || (pit[2].mode == 3) test in the original
 * implementation, it doesn't seem to cause problems though.
 *
 * The implementation of speaker_on & speaker_off can be found in
 * src/base/speaker.c
 *
 * Major Changes from version written by Rainter Zimmerman.
 *
 * o Added support for programs that control the directly through bit 1
 *   of port61.
 *
 * o Added a generic interface to allow multiple speaker backends.
 *
 * o Implemented X speaker code so the emulated speaker now works in X.
 *
 * --EB 21 September 1997
 * DANG_END_FUNCTION
 */
void do_sound(Bit16u period)
{
	/* Note I assume that a sound before after another will kill
	 * the previous sound I had a hard time getting X to do that.
	 * If it becomes a problem possibly tuning sound_duration is
	 * the answer.  I suggest 200ms but as I don't have that
	 * problem now I'm not worring about it.
	 *
	 * But if you set it too low, then sounds can be cut off - clarence
	 */
	static const unsigned sound_duration = 30000;  /* in miliseconds */
	switch (port61 & 3) {
	case 3:		/* speaker on & speaker control through timer channel 2 */
		if ((pit[2].mode == 2) || (pit[2].mode == 3)) {		/* is this test needed? */
			speaker_on(sound_duration,period);
		}
		else {
			speaker_off();	/* is this correct? */
		}
		break;
	case 2:		/* speaker on & direct speaker through bit 1 */
		speaker_on(sound_duration, 0xfff); /* on as long as possible */
		break;
	case 1:		/* speaker off & speaker control through timer channel 2 */
	case 0:		/* speaker off & direct speaker through bit 1 */
		speaker_off();
		break;
	}
}


static void pit_latch(int latch)
{
  hitimer_u cur_time;
  u_long ticks=0;
  pit_latch_struct *p = &pit[latch];

  /* check for special 'read latch status' mode */
  if (p->mode & 0x80) {
    /*
     * Latch status:
     *   bit 7   = state of OUT pin
     *   bit 6   = null count flag (1 == no cntr set, 0 == cntr available)
     *   bit 4-5 = read latch format
     *   bit 1-3 = read latch mode
     *   bit 0   = BCD flag (1 == BCD, 0 == 16-bit -- always 0)
     */
    p->read_latch = (p->read_state << 4) |
                    ((p->mode & 7) << 1) |
                     p->outpin;
    if (p->cntr == -1)
      p->read_latch |= 0x40;
    p->mode &= ~0x80;
    return;	/* let bit 7 on */
  }

  cur_time.td = GETtickTIME(0);

  if ((p->mode & 2)==0) {
    /* non-periodical modes 0,1,4,5 - used mainly by games
     * count down to 0, then set output, wrap and continue counting (gate=1)
     * only modes 0,4 allow reloading of the counter on the fly
     * (thus modes 1,5 are not correctly implemented)
     * we are just not interested in the gate/output pins...
     */
    /* mode 0   -- interrupt on terminal count, ctr reload=Y */
    /* mode 4   -- software triggered pulse, ctr reload=Y */
    /* mode 1   -- programmable monoflop, ctr reload=N */
    /* mode 5   -- hardware triggered pulse, ctr reload=N */
      /* should have been initialized to the value in write_latch */

      if (p->cntr != -1) {
	ticks = (cur_time.td - p->time.td);

	if (ticks > p->cntr) {	/* time has elapsed */
	  if ((p->mode&0x40)==0)
	  	p->cntr = -1;
	  p->outpin = (p->mode&4? 0x00: 0x80);
	} else {
	  p->read_latch = p->cntr - ticks;
	  p->outpin = (p->mode&4? 0x80: 0x00);
	}
      }
      if (p->cntr == -1) {
	p->read_latch = p->write_latch;
	p->outpin = (p->mode&4? 0x00: 0x80);
      }
  }
  else {
    /* mode 2 -- rate generator */
    /* mode 6 -- ??? */
    /* mode 3 -- square-wave generator, countdown by 2 */
    /* mode 7 -- ??? */
      pic_sys_time = cur_time.td;	/* full counter */
    /* NEVER is a special invalid value... skip it if found */
      pic_sys_time += (pic_sys_time == NEVER);

      if (latch == 0) {
	/* when current time is greater than irq time, call pic_request
	   which will then point pic_itime to next interrupt */
#if 0
	if (((p->mode&0x40)==0) && (pic_sys_time > pic_itime[PIC_IRQ0])) {
	  if (pic_request(PIC_IRQ0)==PIC_REQ_OK)
	   { r_printf("PIT: pit_latch, pic_request IRQ0 mode 2/3\n"); }
	}
#endif
	/* while current time is less than next irq time, ticks decrease;
         * ticks can go out of bounds or negative when the interrupt
         * is lost or pending */
	ticks = (pic_itime[VTMR_PIT] - pic_sys_time) % p->cntr;
      } else {
	ticks = p->cntr - (pic_sys_time % p->cntr);
      }

      if ((p->mode & 3)==3) {
        /* ticks is now a value which decreases from cntr to 0, and
           is greater than cntr/2 in the first half of the cycle */
	ticks *= 2;
	if (ticks >= p->cntr) {
	  p->outpin = 0x00; p->read_latch = (ticks-p->cntr) & 0xfffe;
	}
	else {
	  p->outpin = 0x80; p->read_latch = ticks;
	}
      }
      else {
        p->read_latch = ticks;
        p->outpin = (p->read_latch? 0x80: 0x00);
      }
  }

#ifdef DEBUG_PIT
  i_printf("PIT%d: ticks=%lx latch=%x pin=%d\n",latch,ticks,
	p->read_latch,(p->outpin!=0));
#endif
}

/* This is called also by port 0x61 - some programs can use timer #2
 * as a GP timer and read bit 5 of port 0x61 (e.g. Matrox BIOS)
 */
Bit8u pit_inp(ioport_t port)
{
  int ret = 0;
  port -= 0x40;

  if ((port == 2) && (config.speaker == SPKR_NATIVE)) {
	error ("pit_inp() - how could we come here if we defined PORT_FAST?\n");
	return safe_port_in_byte(0x42);
  }
  else if (port == 1)
    i_printf("PIT:  someone is reading the CMOS refresh time?!?");

  if (pit[port].read_latch == -1)
    pit_latch(port);

  switch (pit[port].read_state) {
    case 0: /* read MSB & return to state 3 */
      ret = (pit[port].read_latch >> 8) & 0xff;
      pit[port].read_state = 3;
      pit[port].read_latch = -1;
      break;
    case 3: /* read LSB followed by MSB */
      ret = (pit[port].read_latch & 0xff);
      if (pit[port].mode & 0x80) pit[port].mode &= 7;	/* moved here */
        else
      pit[port].read_state = 0;
      break;
    case 1: /* read MSB */
      ret = (pit[port].read_latch >> 8) & 0xff;
      pit[port].read_latch = -1;
      break;
    case 2: /* read LSB */
      ret = (pit[port].read_latch & 0xff);
      pit[port].read_latch = -1;
      break;
  }
#ifdef DEBUG_PIT
  i_printf("PORT: pit_inp(0x%x) = 0x%x\n", port+0x40, ret);
#endif
  return ret;
}

void pit_outp(ioport_t port, Bit8u val)
{

  port -= 0x40;
  if (port == 1)
    i_printf("PORT: someone is writing the CMOS refresh time?!?");
  else if (port == 2 && config.speaker == SPKR_NATIVE) {
    error ("pit_outp() - how could we come here if we defined PORT_FAST?\n");
    safe_port_out_byte(0x42, val);
    return;
  }

  switch (pit[port].write_state) {
    case 0:
      pit[port].write_latch = pit[port].write_latch | ((val & 0xff) << 8);
      /*
       * TRICK: some graphics apps use the vertical retrace bit to
       * calibrate themselves. AFAIK this is done by starting PIT#0
       * with a value of 0 after detecting the sync, then reading it
       * again at next sync pulse. In this special case, synchronizing
       * the emulated vertical retrace with the PIT write yields some
       * better timing results. It works under X and with emuretrace -- AV
       */
      if (pit[port].write_latch==0) t_vretrace = pic_sys_time;
      pit[port].write_state = 3;
      break;
    case 3:
      pit[port].write_latch = val & 0xff;
      pit[port].write_state = 0;
      break;
    case 1:
      pit[port].write_latch = val & 0xff;
      break;
    case 2:
      pit[port].write_latch = (val & 0xff) << 8;
      break;
    }
#ifdef DEBUG_PIT
  i_printf("PORT: pit_outp(0x%x, 0x%x)\n", port+0x40, val);
#endif

  if (pit[port].write_state != 0) {
    if (pit[port].write_latch == 0)
      pit[port].cntr = 0x10000;
    else
      pit[port].cntr = pit[port].write_latch;

    pit[port].time.td = GETtickTIME(0);

    if (port == 0) {
      ticks_accum   = 0;
      timer_div     = (pit[0].cntr * 10000) / PIT_TICK_RATE;
      if (timer_div == 0)
	timer_div = 1;
#if 1
      i_printf("timer_interrupt_rate count %i, requested %.3g Hz, granted %.3g Hz\n",
	       pit[0].cntr, PIT_TICK_RATE/(double)pit[0].cntr, 10000.0/timer_div);
#endif
    }
#if 0
    else if (port == 2 && config.speaker == SPKR_EMULATED) {
      do_sound(pit[2].write_latch & 0xffff);
    }
#endif
  }
}

Bit8u pit_control_inp(ioport_t port)
{
  return 0;
}

void pit_control_outp(ioport_t port, Bit8u val)
{
  int latch = (val >> 6) & 0x03;

#ifdef DEBUG_PIT
  i_printf("PORT: outp(0x43, 0x%x)\n",val);
#endif

  /* Timer commands (00-BF):
   *  bit 6-7 = Timer (0,1,2)
   *		3 is not a timer but a special read-back command
   *
   *    bit 4-5 = command 00=latch 01=LSB mode 10=MSB mode 11=16bit mode
   *    bit 1-3 = mode selection
   *                    mode 0(000) 1(001) 2(010 or 110) 3(011 or 111)
   *                         4(100) 5(101)
   *			6(110) and 7(111) undefined?
   *    bit 0   = binary(0), BCD(1)
   *
   *  0xh	counter latch timer 0
   *  1xh	timer 0 LSB mode
   *			modes: 0,2,3,4 - 1,5 not applicable
   *  2xh	timer 0 MSB mode
   *			modes: 0,2,3,4 - 1,5 not applicable
   *  3xh	timer 0 16bit mode
   *			modes: 0,2,4 - 3 typical - 1,5 not applicable
   *  modes 1,5 applicable only to timer 2 [van Gilluwe,1994]
   */
  switch (latch) {
    case 2:
      if (config.speaker == SPKR_NATIVE) {
        safe_port_out_byte(0x43, val);
	break;
      }
      /* nobreak; */
    case 0:
    case 1:
      if ((val & 0x30) == 0)
	pit_latch(latch);
      else {
	pit[latch].read_state  = (val >> 4) & 0x03;
	pit[latch].write_state = (val >> 4) & 0x03;
	pit[latch].mode        = (val >> 1) & 0x07;
        if ((val & 4)==0) {      /* modes 0,1,4,5 */
          /* set the time base for the counter - safety code for programs
           * which use a non-periodical mode without reloading the counter
           */
          pit[latch].time.td = GETtickTIME(0);
        }
      }
#ifdef DEBUG_PIT
      i_printf("PORT: writing outp(0x43, 0x%x)\n", val);
#endif
      break;
    case 3:
      /* I think this code is more or less correct */
      if ((val & 0x20) == 0) {        /* latch counts? */
	if (val & 0x02) pit_latch(0);
	if (val & 0x04) pit_latch(1);
	if (val & 0x08) pit_latch(2);
      }
      else if ((val & 0x10) == 0) {   /* latch status words? */
	int or_mask = ((val & 0x20) == 0 ? 0xc0 : 0x80);
	if (val & 0x02) { pit[0].mode |= or_mask; pit_latch(0); }
	if (val & 0x04) { pit[1].mode |= or_mask; pit_latch(1); }
	if (val & 0x08) { pit[2].mode |= or_mask; pit_latch(2); }
      }
      break;
  }
}

/* DANG_BEGIN_FUNCTION pic_activate
 *
 * pic_activate requests any interrupts whose scheduled time has arrived.
 * anything after pic_dos_time and before pic_sys_time is activated.
 * pic_dos_time is advanced to the earliest time scheduled.
 * DANG_END_FUNCTION
 */
static void pic_activate(void)
{
  hitimer_t earliest;
  int timer, count;

  if (pic_pending())
    return;

/*if(pic_irr&~pic_imr) return;*/
   earliest = pic_sys_time;
   count = 0;
   for (timer=0; timer<32; ++timer) {
      if ((pic_itime[timer] != NEVER) && (pic_itime[timer] < pic_sys_time)) {
         if (pic_itime[timer] != pic_ltime[timer]) {
               if ((earliest == NEVER) || (pic_itime[timer] < earliest))
                    earliest = pic_itime[timer];
               pic_ltime[timer] = pic_itime[timer];
               vtmr_raise(timer);
               ++count;
         } else {
               r_printf("pic_itime and pic_ltime for timer %i matched!\n", timer);
         }
      }
   }
   if(count) r_printf("Activated %i interrupts.\n", count);
   r_printf("Activate ++ dos time to %li\n", earliest);
   r_printf("pic_sys_time is %li\n", pic_sys_time);
   pic_itime[32] = earliest;
   if (count)
      pic_dos_time = earliest;
}

int timer_get_vpend(int timer)
{
    if (timer != 0 || pit[timer].cntr == 0 || pic_itime[timer] > pic_sys_time)
        return 0;
    return ((pic_sys_time - pic_itime[timer]) / pit[timer].cntr + 1);
}

/* DANG_BEGIN_FUNCTION pic_sched
 * pic_sched schedules an interrupt for activation after a designated
 * time interval.  The time measurement is in unis of 1193047/second,
 * the same rate as the pit counters.  This is convenient for timer
 * emulation, but can also be used for pacing other functions, such as
 * serial emulation, incoming keystrokes, or video updates.  Some sample
 * intervals:
 *
 * rate/sec:	5	7.5	11	13.45	15	30	60
 * interval:	238608	159072	108459	88702	79536	39768	19884
 *
 * rate/sec:	120	180	200	240	360	480	720
 * interval:	9942	6628	5965	4971	3314	2485	1657
 *
 * rate/sec:	960	1440	1920	2880	3840	5760	11520
 * interval:	1243	829	621	414	311	207	103
 *
 * pic_sched expects two parameters: an interrupt level and an interval.
 * To assure proper repeat scheduling, pic_sched should be called from
 * within the interrupt handler for the same interrupt.  The maximum
 * interval is 15 minutes (0x3fffffff).
 * DANG_END_FUNCTION
 */

static void pic_sched(int ilevel, int interval)
{
  char mesg[35];

 /* default for interval is 65536 (=54.9ms)
  * There's a problem with too small time intervals - an interrupt can
  * be continuously scheduled, without letting any time to process other
  * code.
  *
  * BIG WARNING - in non-periodic timer modes pit[0].cntr goes to -1
  *	at the end of the interval - was this the reason for the following
  *	[1u-15sec] range check?
  */
  if(interval > 0 && interval < 0x3fffffff) {
     if(pic_ltime[ilevel]==NEVER) {
	pic_itime[ilevel] = pic_sys_time + interval;
     } else {
	pic_itime[ilevel] = pic_itime[ilevel] + interval;
     }
  }
  if (debug_level('r') > 2) {
    /* avoid going through sprintf for non-debugging */
    sprintf(mesg,", delay= %d.",interval);
    r_printf("Scheduling timer %i%s\n", ilevel, mesg);
    r_printf("pic_itime set to %li\n", pic_itime[ilevel]);
  }

  pic_activate();
}

/* DANG_BEGIN_FUNCTION pic_watch
 *
 * pic_watch is a watchdog timer for pending interrupts.  If pic_iret
 * somehow fails to activate a pending interrupt request for 2 consecutive
 * timer ticks, pic_watch will activate them anyway.  pic_watch is called
 * ONLY by timer_tick, the interval timer signal handler, so the two functions
 * will probably be merged.
 *
 * DANG_END_FUNCTION
 */
static void pic_watch(hitimer_u *s_time)
{
  hitimer_t t_time;

/*  calculate new sys_time
 *  values are kept modulo 2^32 (exactly 1 hour)
 */
  t_time = s_time->td;

  /* check for any freshly initiated timers, and sync them to s_time */
  r_printf("pic_itime[1]=%li\n", pic_itime[1]);
  pic_sys_time=t_time + (t_time == NEVER);
  r_printf("pic_sys_time set to %li\n", pic_sys_time);
  pic_dos_time = pic_itime[32];

  pic_activate();
}

static void timer_irq_ack(int masked)
{
  pic_sched(0, pit[0].cntr);
}

/* reads/writes to the speaker control port (0x61)
 * Port 0x61 is really more complex than a speaker enable bit... look here:
 * [output]:
 * Bit(s)	Description
 *  7	pulse to 1 for IRQ1 reset (PC,XT)
 *  6-4	reserved
 *  3	I/O channel parity check disable
 *  2	RAM parity check disable
 *  1	speaker data enable
 *  0	timer 2 gate to speaker enable
 *
 * [input]:
 * Bit(s)	Description
 *  7	RAM parity error occurred
 *  6	I/O channel parity error occurred
 *  5	mirrors timer 2 output condition
 *  4	toggles with each refresh request
 *  3	NMI I/O channel check status
 *  2	NMI parity check status
 *  1	speaker data status
 *  0	timer 2 clock gate to speaker status
 */
Bit8u spkr_io_read(ioport_t port) {
   if (port==0x61)  {
      if (config.speaker == SPKR_NATIVE)
         return port_safe_inb(0x61);
      else {
	 /* keep the connection between port 0x61 and PIT timer#2 */
	 pit_latch(2);
         return ((*((Bit8u *)&pic_sys_time)&0x10) | /* or anything that toggles quick enough */
		(pit[2].outpin? 0x20:0) |	/* outpin: 00 or 80 */
		(port61&0xcf));
      }
   }
   return 0xff;
}

void spkr_io_write(ioport_t port, Bit8u value) {
   if (port==0x61) {
      switch (config.speaker) {
       case SPKR_NATIVE:
          port_safe_outb(0x61, value & 0x03);
          break;

       case SPKR_EMULATED:
	  if ((value & 3) == (port61 & 3))
	    break;
	  port61 = value & 0x0f;
          do_sound(pit[2].write_latch & 0xffff);
	  break;

       case SPKR_OFF:
          port61 = value & 0x0c;
          break;
      }
   }
}

void pit_init(void)
{
  emu_iodev_t  io_device;

  /* 8254 PIT (Programmable Interval Timer) */
  io_device.read_portb   = pit_inp;
  io_device.write_portb  = pit_outp;
  io_device.read_portw   = NULL;
  io_device.write_portw  = NULL;
  io_device.read_portd   = NULL;
  io_device.write_portd  = NULL;
  io_device.handler_name = "8254 Timer0";
  io_device.start_addr   = 0x0040;
  io_device.end_addr     = 0x0040;
  port_register_handler(io_device, 0);

  io_device.handler_name = "8254 Timer1";
  io_device.start_addr = 0x0041;
  io_device.end_addr = 0x0041;
  port_register_handler(io_device, 0);

  io_device.handler_name = "8254 Timer2";
  io_device.start_addr = 0x0042;
  io_device.end_addr = 0x0042;
  port_register_handler(io_device, config.speaker==SPKR_NATIVE? PORT_FAST:0);

  io_device.read_portb   = pit_control_inp;
  io_device.write_portb  = pit_control_outp;
  io_device.handler_name = "8254 Ctrl02";
  io_device.start_addr   = 0x0043;
  io_device.end_addr     = 0x0043;
  port_register_handler(io_device, 0);

  /* register_handler for port 0x61 is in keyboard code */
  port61 = 0x0c;
#if 0
  io_device.start_addr   = 0x0047;
  io_device.end_addr     = 0x0047;
  port_register_handler(io_device, 0);
#endif

  vtmr_register(VTMR_PIT, timer_irq_ack);
  vtmr_set_tweaked(VTMR_PIT, config.timer_tweaks, 0);
}

void pit_reset(void)
{
  hitimer_t cur_time;

  cur_time = GETtickTIME(0);

  pit[0].mode        = 3;
  pit[0].outpin      = 0;
  pit[0].cntr        = 0x10000;
  pit[0].time.td     = cur_time;
  pit[0].read_latch  = 0xffffffff;
  pit[0].write_latch = 0;
  pit[0].read_state  = 3;
  pit[0].write_state = 3;

  pit[1].mode        = 2;
  pit[1].outpin      = 0;
  pit[1].cntr        = 18;
  pit[1].time.td     = cur_time;
  pit[1].read_latch  = 0xffffffff;
  pit[1].write_latch = 18;
  pit[1].read_state  = 3;
  pit[1].write_state = 3;

  pit[2].mode        = 0;
  pit[2].outpin      = 0;
  pit[2].cntr        = 0x10000;
  pit[2].time.td     = cur_time;
  pit[2].read_latch  = 0xffffffff;
  pit[2].write_latch = 0;
  pit[2].read_state  = 3;
  pit[2].write_state = 3;

  pit[3].mode        = 0;
  pit[3].outpin      = 0;
  pit[3].cntr        = 0x10000;
  pit[3].time.td     = cur_time;
  pit[3].read_latch  = 0xffffffff;
  pit[3].write_latch = 0;
  pit[3].read_state  = 3;
  pit[3].write_state = 3;

  port61 = 0x0c;
}

void pit_late_init(void)
{
  pic_ltime[0] = pic_itime[0] = pic_sys_time;
  vtmr_raise(0);  /* start timer */
}

#define TIMER0_FLOOD_THRESHOLD 50000

int CAN_SLEEP(void)
{
  return (!(pic_get_isr() || (REG(eflags) & VIP) || signal_pending() ||
    (pic_sys_time > pic_dos_time + TIMER0_FLOOD_THRESHOLD) || in_leavedos));
}
