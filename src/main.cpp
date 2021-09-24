/*** includes ***/
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

/*** data ***/
struct termios orig_termios;

/*** terminal ***/
void die(const char* s)
{
    perror(s);
    exit(1);
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

/**
 * @brief enable raw mode on the terminal
 */
void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(
        ECHO | // no print to terminal
        ICANON | // not canonical mode
        IEXTEN | // turn off Ctrl-V
        ISIG // turn off Ctrl-C, Z
    );
    raw.c_iflag &= ~(
        BRKINT | // a break condition does not cause a SIGINT signal
        INPCK | // not enable parity check
        ISTRIP | // not cause the 8th bit of each input byte to be stripped(0)
        ICRNL | // not translate 'carriage return'('\r') to 'newlines'('\n')
        IXON // turn off Ctrl-S, Q
    );
    raw.c_cflag |= (CS8); // set character size to 8bits per byte
    raw.c_oflag &= ~(
        OPOST // not translate '\n' to "\r\n"
    );
    raw.c_cc[VMIN] = 0; // mimumum number of bytes of input needed before read()
    raw.c_cc[VTIME] = 1; // maximum amount of time to wait before read() returns in 100ms

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/*** init ***/
int main(int argc, const char *argv[])
{
    enableRawMode();

    while (true) {
        char c = '\0';
        if (
            read(STDIN_FILENO, &c, 1) == -1 &&
            errno != EAGAIN // errno will be set EAGAIN on timeout in Cygwin
        )
            die("read");

        if (iscntrl(c)) { // check c whether if it's printable
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    }

    return 0;
}
