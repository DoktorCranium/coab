/* input.h - INT 16h keyboard semantics and delays.
 * Ported from engine/seg049.cs and the input half of engine/seg043.cs.
 *
 * Extended keys (arrows, page up/down, ...) have a zero ASCII byte. DOS
 * reported those as two reads: first a 0, then the scan code. READKEY keeps
 * that behaviour because the engine's menu code depends on it.
 */
#ifndef COAB_INPUT_H
#define COAB_INPUT_H

#include "coab.h"

/* seg049.SysDelay */
void input_sys_delay(int milliseconds);

/* seg049.KEYPRESSED - true if a read would return immediately. */
bool input_key_pressed(void);

/* seg049.READKEY */
u8 input_read_key(void);

/* seg043.GetInputKey - READKEY plus the engine's housekeeping: silence sounds
 * on Ctrl+S, honour the keyboard-exit cheat, and drain any keys that piled up
 * behind the one being returned. */
u8 input_get_key(void);

/* seg043.clear_keyboard / clear_one_keypress */
void input_clear_keyboard(void);
void input_clear_one_keypress(void);

/* True once the player has closed the window. The engine checks this at its
 * wait points so it can unwind and save instead of being killed. */
bool input_quit_requested(void);

#endif /* COAB_INPUT_H */
