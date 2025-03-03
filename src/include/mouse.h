/* mouse support for dosemu 0.48p2+
 *     Robert Sanders, gt8134b@prism.gatech.edu
 */
#ifndef MOUSE_H
#define MOUSE_H

#include <termios.h>

#define MOUSE_BASE_VERSION	0x0700	/* minimum driver version 7.00 */
#define MOUSE_EMU_VERSION	0x0005	/* my driver version 0.05 */
/* this is the version returned to DOS programs */
#define MOUSE_VERSION	  (MOUSE_BASE_VERSION + MOUSE_EMU_VERSION)

enum {  MOUSE_NONE = -1,
        MOUSE_MICROSOFT,
        MOUSE_MS3BUTTON,
        MOUSE_MOUSESYSTEMS,
        MOUSE_MMSERIES,
        MOUSE_LOGITECH,
        MOUSE_BUSMOUSE,
        MOUSE_MOUSEMAN,
        MOUSE_PS2,
        MOUSE_HITACHI,
        MOUSE_X,
        MOUSE_IMPS2,
        MOUSE_XTERM,
        MOUSE_GPM,
        MOUSE_SDL,
        MOUSE_SDL1,
        MOUSE_VKBD,
};

/* types of mouse events */
#define DELTA_CURSOR		1
#define DELTA_LEFTBDOWN		2	/* pressed  */
#define DELTA_LEFTBUP		4	/* released */
#define DELTA_RIGHTBDOWN	8
#define DELTA_RIGHTBUP		16
#define DELTA_MIDDLEBDOWN	32
#define DELTA_MIDDLEBUP		64
#define DELTA_WHEEL		128
#define DELTA_ABSOLUTE		256

#define HEIGHT 16
#define PLANES 4

typedef struct  {
  char *dev;
  int com;
  int com_num;
  int fd;
  int type;
  int dev_type;
  int flags;
  boolean intdrv;
  /* whether we use the native DOS cursor, or the system cursor (X, GPM) */
  boolean native_cursor;
  boolean emulate3buttons;
  boolean has3buttons;
  boolean cleardtr;
  int baudRate;
  int sampleRate;
  int lastButtons;
  int chordMiddle;
  int init_speed_x, init_speed_y;
  int ignorevesa;
  int ignore_speed;

  struct termios *oldset;
} mouse_t;

/* this entire structure gets saved on a driver state save */
/* therefore we try to keep it small where appropriate */
struct mouse_struct {
  boolean enabled;
  unsigned char lbutton, mbutton, rbutton;
  int lpcount, lrcount, mpcount, mrcount, rpcount, rrcount;
  int16_t wmcount;

  /* positions for last press/release for each button */
  int lpx, lpy, mpx, mpy, rpx, rpy;
  int lrx, lry, mrx, mry, rrx, rry;
  int wmx, wmy;

  /* TRUE if we're in a graphics mode */
  boolean gfx_cursor;

  unsigned char xshift, yshift;

  /* text cursor definition (we only support software cursor) */
  unsigned short textscreenmask, textcursormask;

  /* graphics cursor defnition */
  signed char hotx, hoty;
  unsigned short graphscreenmask[16], graphcursormask[16];

  /* exclusion zone */
  int exc_ux, exc_uy, exc_lx, exc_ly;

  int px_abs, py_abs, px_range, py_range;
  int need_resync;
  /* for abs movement correction */
  int x_delta, y_delta;
  /* unscaled ones, to not loose the precision - these need to be int to avoid overflowing 16 bits */
  long long unsc_x, unsc_y;
  long long unscm_x, unscm_y;
  /* coordinates at which the cursor was last drawn */
  int oldrx, oldry;
  /* these are the cursor extents; they are rounded off. */
  int maxx, maxy;
  /* same as above except can be played with */
  int virtual_minx, virtual_maxx, virtual_miny, virtual_maxy;

  /* these are for sensitivity options */
  int speed_x, speed_y;
  int sens_x, sens_y;
  int threshold;
  int language;

  /* zero if cursor is on, negative if it's off */
  int cursor_on;
  int visibility_locked;
  int visibility_changed;

  /* this is for the user-defined subroutine */
  unsigned short cs, ip;
  unsigned short mask;

  /* true if mouse has three buttons (third might be emulated) */
  boolean threebuttons;

  int display_page;

  /* These are to enable work arounds for broken applications */
  int min_max_x, min_max_y;

  int win31_mode;

  struct {
    boolean state;
    unsigned short pkg;
    unsigned short cs, ip;
  } ps2;
};
extern struct mouse_struct mouse;

struct mouse_client {
  const char *name;
  int    (*init)(void);
  void   (*close)(void);
  void   (*show_cursor)(int yes);
  void   (*post_init)(void);
  int    (*reset)(void);
};

void register_mouse_client(struct mouse_client *mouse);
void mouse_client_show_cursor(int yes);
void mouse_client_close(void);
void mouse_client_post_init(void);
void mouse_client_reset(void);

extern struct mouse_client Mouse_raw;

#include "keyboard/keyboard.h"
void mouse_keyboard(Boolean make, t_keysym key);

extern void mouse_priv_init(void);
extern void dosemu_mouse_init(void);
extern void mouse_late_init(void);
extern void mouse_ps2bios(void);
extern int mouse_int(void);
extern void dosemu_mouse_close(void);
extern void freeze_mouse(void);
extern void unfreeze_mouse(void);
extern void int74(void);

int DOSEMUMouseProtocol(unsigned char *rBuf, int nBytes, int type,
	const char *id);

struct mouse_drv {
  int  (*init)(void);
  int  (*accepts)(int from, void *udata);
  void (*move_button)(int num, int press, void *udata);
  void (*move_buttons)(int lbutton, int mbutton, int rbutton, void *udata);
  void (*move_wheel)(int dy, void *udata);
  void (*move_relative)(int dx, int dy, int x_range, int y_range, void *udata);
  void (*move_mickeys)(int dx, int dy, void *udata);
  void (*move_absolute)(int x, int y, int x_range, int y_range, int vis,
      void *udata);
  void (*drag_to_corner)(int x_range, int y_range, void *udata);
  void (*enable_native_cursor)(int flag, void *udata);
  const char *name;
};

void register_mouse_driver(struct mouse_drv *mouse);
void register_mouse_driver_buffered(struct mouse_drv *mouse,
    void (*callback)(void *));
void mousedrv_set_udata(const char *name, void *udata);
int mousedrv_process_fifo(const char *name);

void mouse_move_button(int num, int press, int from);
void mouse_move_buttons(int lbutton, int mbutton, int rbutton, int from);
void mouse_move_wheel(int dy, int from);
void mouse_move_relative(int dx, int dy, int x_range, int y_range, int from);
void mouse_move_mickeys(int dx, int dy, int from);
void mouse_move_absolute(int x, int y, int x_range, int y_range, int vis,
	int from);
void mouse_drag_to_corner(int x_range, int y_range, int from);
void mouse_enable_native_cursor(int flag, int from);

void mouse_move_button_id(int num, int press, const char *id);
void mouse_move_buttons_id(int lbutton, int mbutton, int rbutton,
	const char *id);
void mouse_move_wheel_id(int dy, const char *id);
void mouse_move_mickeys_id(int dx, int dy, const char *id);
void mouse_enable_native_cursor_id(int flag, const char *id);
int mousedrv_accepts(const char *id, int from);

extern void mouse_helper(struct vm86_regs *);
extern void mouse_set_win31_mode(void);

#endif /* MOUSE_H */
