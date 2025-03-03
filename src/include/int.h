#ifndef INT_H
#define INT_H

#include <stdint.h>
#include <time.h> /* for time_t */

#define WINDOWS_HACKS 1
#if WINDOWS_HACKS
enum win3x_mode_enum { INACTIVE, RM, STANDARD, ENHANCED };
extern enum win3x_mode_enum win3x_mode;
#endif

extern unsigned int  check_date;
extern time_t        start_time;

extern uint32_t int_bios_area[0x500/sizeof(uint32_t)];

void do_int(int);
void fake_int(int, int);
void fake_int_to(int cs, int ip);
void fake_call(int, int);
void fake_call_to(int cs, int ip);
void fake_pusha(void);
void fake_retf(void);
void fake_iret(void);
void do_eoi_iret(void);
void do_eoi2_iret(void);
void do_iret(void);
void jmp_to(int cs, int ip);
void setup_interrupts(void);
void version_init(void);
void dos_post_boot_reset(void);
void int_try_disable_revect(void);

enum { I_NOT_HANDLED, I_HANDLED, I_SECOND_REVECT };

extern int can_revector(int i);
far_t get_int_vector(int vec);

void update_xtitle(void);

void int42_hook(void);

int *add_syscom_drive(char *path);
int add_extra_drive(char *path, int ro, int cd, int grp);
int find_free_drive(void);
uint16_t get_redirection(uint16_t redirIndex, char *deviceStr, int deviceSize,
    char *resourceStr, int resourceSize, uint16_t *deviceUserData,
    uint16_t *deviceOptions, uint8_t *deviceStatus);
uint16_t get_redirection_ux(uint16_t redirIndex, char *deviceStr, int deviceSize,
    char *resourceStr, int resourceSize, uint16_t *deviceUserData,
    uint16_t *deviceOptions, uint8_t *deviceStatus);
int get_lastdrive(void);
int getCWD_r(int drive, char *rStr, int len);
int getCWD_cur(char *rStr, int len);
char *getCWD(int drive);
int get_redirection_root(int drive, char *presourceStr, int resourceLength);
int is_redirection_ro(int drive);

#endif
