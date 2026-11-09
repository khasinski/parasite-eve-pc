/* No controller in the host harness: the game's own input schedule
 * (g_BootHostPadEvents) presses the buttons. Port 0 is a released digital
 * pad, port 1 is disconnected. */
#include <psyz.h>
#include <string.h>
void MyPadInit(int mode) { (void)mode; }
void Psyz_PadsPoll(void) {
    char frame[PSYZ_PAD_BUF_LEN];
    memset(frame, 0, sizeof frame);
    frame[1] = (char)PSYZ_CTRL_DIGITAL_PAD;
    frame[2] = frame[3] = (char)0xFF;
    Psyz_PadsSet(0, frame, sizeof frame);
    memset(frame, 0xFF, sizeof frame);
    Psyz_PadsSet(1, frame, sizeof frame);
}
