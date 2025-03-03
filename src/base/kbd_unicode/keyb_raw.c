/*
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 */

#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_KD_H
#include <sys/kd.h>
#endif
#include <sys/ioctl.h>

#include "emu.h"
#include "keyboard.h"
#include "keyb_clients.h"
#include "translate/keysym_attributes.h"
#include "keystate.h"

#define KBBUF_SIZE (KEYB_QUEUE_LENGTH / 2)

/* LED FLAGS (from Linux keyboard code) */
#define LED_SCRLOCK	0
#define LED_NUMLOCK	1
#define LED_CAPSLOCK	2

static struct termios save_termios;	/* original terminal modes */
#ifdef HAVE_SYS_KD_H
static int save_mode;                   /* original keyboard mode  */
#endif
static int kbd_fd = -1;

static void set_kbd_leds(t_modifiers shiftstate)
{
#ifdef HAVE_SYS_KD_H
  unsigned int led_state = 0;
  static t_modifiers prev_shiftstate = 0xffff;

  if (shiftstate==prev_shiftstate) return;
  prev_shiftstate=shiftstate;

  if (shiftstate & MODIFIER_SCR) {
    led_state |= (1 << LED_SCRLOCK);
  }
  if (shiftstate & MODIFIER_NUM) {
    led_state |= (1 << LED_NUMLOCK);
  }
  if (shiftstate & MODIFIER_CAPS) {
    led_state |= (1 << LED_CAPSLOCK);
  }
  k_printf("KBD(raw): kbd_set_leds() setting LED state\n");
  ioctl(kbd_fd, KDSETLED, led_state);
#endif
}

static t_shiftstate get_kbd_flags(void)
{
  t_modifiers s = 0;
#ifdef HAVE_SYS_KD_H
  int rc;
  unsigned int led_state = 0;

  k_printf("KBD(raw): getting keyboard flags\n");

  /* note: this reads the keyboard flags, not the LED state (which would
   * be KDGETLED).
   */
  rc = ioctl(kbd_fd, KDGKBLED, &led_state);
  if (rc == -1)
    return 0;

  if (led_state & (1 << LED_SCRLOCK))  s|=MODIFIER_SCR;
  if (led_state & (1 << LED_NUMLOCK))  s|=MODIFIER_NUM;
  if (led_state & (1 << LED_CAPSLOCK)) s|=MODIFIER_CAPS;
#endif
  return s;
}

static int use_move_key(t_keysym key)
{
	int result = FALSE;
	/* If it's some kind of function key move it
	 * otherwise just make sure it gets pressed
	 */
	if (is_keysym_function(key) ||
	    is_keysym_dosemu_key(key) ||
	    is_keypad_keysym(key) ||
	    (key == DKY_TAB) ||
	    (key == DKY_RETURN) ||
	    (key == DKY_BKSP) || (key == U_DELETE)) {
		result = TRUE;
	}
	return result;
}

static void do_raw_getkeys(int fd, void *arg)
{
  int i,count;
  char buf[KBBUF_SIZE];

  count = RPT_SYSCALL(read(kbd_fd, buf, KBBUF_SIZE - 1));
  k_printf("KBD(raw): do_raw_getkeys() found %d characters (Raw)\n", count);
  if (count == -1) {
    k_printf("KBD(raw): do_raw_getkeys(): keyboard read failed!\n");
    return;
  }
  buf[count] = 0;
  if (config.console_keyb == KEYB_RAW) {
    for (i = 0; i < count; i++) {
      k_printf("KBD(raw): readcode: %02x \n", buf[i]);
      put_rawkey(buf[i]);
    }
  } else {
    const char *p = buf;
    while (*p) {
      int rc;
      t_unicode _key[2];
      #define key _key[0]
      struct char_set_state state;
      init_charset_state(&state, trconfig.keyb_charset);
      rc = charset_to_unicode_string(&state, &key, &p, strlen(p), 2);
      cleanup_charset_state(&state);
      if (rc != 1)
        break;
      if (use_move_key(key)) {
        move_key(1, key);
        move_key(0, key);
      } else {
        put_symbol(1, key);
        put_symbol(0, key);
      }
    }
  }
}

#if 0 /* debug code */
static void print_termios(struct termios term)
{
  k_printf("KBD(raw): TERMIOS Structure:\n");
  k_printf("KBD(raw): 	c_iflag=%lx\n", term.c_iflag);
  k_printf("KBD(raw): 	c_oflag=%lx\n", term.c_oflag);
  k_printf("KBD(raw): 	c_cflag=%lx\n", term.c_cflag);
  k_printf("KBD(raw): 	c_lflag=%lx\n", term.c_lflag);
  k_printf("KBD(raw): 	c_line =%x\n", (int)term.c_line);
}
#endif

static inline void set_raw_mode(void)
{
  struct termios buf = save_termios;

#ifdef HAVE_SYS_KD_H
  if (config.console_keyb == KEYB_RAW) {
    k_printf("KBD(raw): Setting keyboard to RAW mode\n");
    ioctl(kbd_fd, KDSKBMODE, K_RAW);
  }
#endif
  cfmakeraw(&buf);
  k_printf("KBD(raw): Setting TERMIOS Structure.\n");
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &buf) < 0)
    k_printf("KBD(raw): Setting TERMIOS structure failed.\n");

#if 0 /* debug code */
  if (tcgetattr(kbd_fd, &buf) < 0) {
    k_printf("KBD(raw): Termios ERROR\n");
  }
  print_termios(buf);
#endif
}

/*
 * DANG_BEGIN_FUNCTION raw_keyboard_init
 *
 * Initialize the keyboard for RAW mode.
 *
 * DANG_END_FUNCTION
 */
static int raw_keyboard_init(void)
{
  if (config.console_keyb > KEYB_TTY)
    return FALSE;

  k_printf("KBD(raw): raw_keyboard_init()\n");

  kbd_fd = STDIN_FILENO;
#ifdef HAVE_SYS_KD_H
  if (config.console_keyb == KEYB_RAW)
    ioctl(kbd_fd, KDGKBMODE, &save_mode);
#endif
  if (tcgetattr(kbd_fd, &save_termios) < 0) {
    error("KBD(raw): Couldn't tcgetattr(kbd_fd,...) !\n");
    memset(&save_termios, 0, sizeof(save_termios));
    return FALSE;
  }

  set_raw_mode();

  add_to_io_select(kbd_fd, do_raw_getkeys, NULL);

  return TRUE;
}

/*
 * DANG_BEGIN_FUNCTION raw_keyboard_reset
 *
 * Reset the keyboard shiftstate to match the keyboard LED's
 *
 * DANG_END_FUNCTION
 */
static void raw_keyboard_reset(void)
{
  /* initialise the server's shift state to the current keyboard state */
  set_shiftstate(get_kbd_flags());

}

/* something like this oughta be defined in linux/kd.h but isn't... */
/* reset LEDs to normal mode, reflecting keyboard state */
#define LED_NORMAL 0x08

static void raw_keyboard_close(void)
{
  if (kbd_fd != -1) {
    k_printf("KBD(raw): raw_keyboard_close: resetting keyboard to original mode\n");
    remove_from_io_select(kbd_fd);
#ifdef HAVE_SYS_KD_H
    if (config.console_keyb == KEYB_RAW) {
      ioctl(kbd_fd, KDSKBMODE, save_mode);

      k_printf("KBD(raw): resetting LEDs to normal mode\n");
      ioctl(kbd_fd, KDSETLED, LED_NORMAL);
    }
#endif
    k_printf("KBD(raw): Resetting TERMIOS structure.\n");
    if (tcsetattr(kbd_fd, TCSAFLUSH, &save_termios) < 0) {
      k_printf("KBD(raw): Resetting keyboard termios failed.\n");
    }
    kbd_fd = -1;
  }
}

static int raw_keyboard_probe(void)
{
	return isatty(STDIN_FILENO);
}

struct keyboard_client Keyboard_raw =  {
   "raw",                      /* name */
   raw_keyboard_probe,	       /* probe */
   raw_keyboard_init,          /* init */
   raw_keyboard_reset,         /* reset */
   raw_keyboard_close,         /* close */
   set_kbd_leds,       	       /* set_leds */
};
