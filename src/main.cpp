/*** includes ***/
#include <ctype.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

constexpr int TAB_SIZE = 8;

enum EditorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/
struct EditorConfig {
    int cursorX, cursorY;  // cursor positions in the file
    int cursorRX;          // cursor position in the render line
    int screenRows, screenCols;
    int rowOffset, colOffset;          // screen position in the file
    std::vector<std::string> lines;    // actual data in the file opened
    std::vector<std::string> renders;  // rendered data on this editor
    std::string filename;
    std::string statusMsg;
    time_t statusMsgTime;
    struct termios orig_termios;
    bool modified;
};

EditorConfig g_E;

/*** terminal ***/
void writeTerminal(const void* buf, size_t n) {
    if (write(STDOUT_FILENO,  // from unistd.h
              buf, n)) {
    } else {
    }
}

void clearScreen(std::string& buf) {
    buf += "\x1b[2J";  // \x1b -> escape character
                       // \x1b[ instructs terminal to format texts
                       // J command to clear screen
                       // 2 for entire sreen
    buf += "\x1b[H";   // H command to position the cursor
}

void die(const char* s) {
    // clearScreen();
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_E.orig_termios) == -1)
        die("tcsetattr");
}

/**
 * @brief enable raw mode on the terminal
 */
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &g_E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = g_E.orig_termios;
    raw.c_lflag &= ~(ECHO |    // no print to terminal
                     ICANON |  // not canonical mode
                     IEXTEN |  // turn off Ctrl-V
                     ISIG      // turn off Ctrl-C, Z
    );
    raw.c_iflag &= ~(
        BRKINT |  // a break condition does not cause a SIGINT signal
        INPCK |   // not enable parity check
        ISTRIP |  // not cause the 8th bit of each input byte to be stripped(0)
        ICRNL |   // not translate 'carriage return'('\r') to 'newlines'('\n')
        IXON      // turn off Ctrl-S, Q
    );
    raw.c_cflag |= (CS8);   // set character size to 8bits per byte
    raw.c_oflag &= ~(OPOST  // not translate '\n' to "\r\n"
    );
    raw.c_cc[VMIN] =
        0;  // mimumum number of bytes of input needed before read()
    raw.c_cc[VTIME] =
        1;  // maximum amount of time to wait before read() returns in 100ms

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int readKey() {
    int c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 &&
        errno != EAGAIN  // errno will be set EAGAIN on timeout in Cygwin
    )
        die("read");

    if (c == '\x1b') {
        char seq[3];

        // try to read two more bytes, if fails input must have been escape key
        if (read(STDOUT_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDOUT_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                // read one more bytes
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DEL_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }
        } else if (seq[0] == '0') {
            switch (seq[1]) {
                case 'H':
                    return HOME_KEY;
                case 'F':
                    return END_KEY;
            }
        }

        return '\x1b';
    }
    return c;
}

int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO,
              "\x1b[6n",  // query terminal info.
                          // argument 6 for cursor position
              4) != 4)
        return -1;

    // read query response
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    // parse string
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int* rows, int* cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(&g_E.screenRows, &g_E.screenCols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void initEditor() {
    g_E.cursorX = 0;
    g_E.cursorY = 0;
    g_E.cursorRX = 0;
    g_E.rowOffset = 0;
    g_E.colOffset = 0;
    g_E.modified = false;
    if (getWindowSize(&g_E.screenRows, &g_E.screenCols) == -1)
        die("getWindowSize");

    g_E.statusMsgTime = 0;

    g_E.screenRows -= 2;  // for status lines
}

std::string convertToRenderingRow(const std::string& line) {
    // replace tab by spaces
    std::string render;
    for (auto& c : line) {
        if (c == '\t')
            for (int i = 0; i < TAB_SIZE; ++i) render.push_back(' ');
        else
            render.push_back(c);
    }
    return render;
}

void editorOpen(const std::string& filename) {
    std::ifstream ifs(filename);
    g_E.filename = filename;
    std::string line;
    while (std::getline(ifs, line)) {
        g_E.lines.emplace_back(line);
        g_E.renders.emplace_back(convertToRenderingRow(line));
    }
}

void editorScroll() {
    if (g_E.cursorY < g_E.lines.size()) {
        // tab key
        int rx = 0;
        for (int i = 0; i < g_E.cursorX; ++i) {
            if (g_E.lines[g_E.cursorY][i] == '\t')
                rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
            rx++;
        }
        g_E.cursorRX = rx;
    }

    if (g_E.cursorRX < g_E.colOffset) {
        g_E.colOffset = g_E.cursorRX;
    }
    if (g_E.cursorRX >= g_E.colOffset + g_E.screenCols) {
        g_E.colOffset = g_E.cursorRX - g_E.screenCols + 1;
    }
    if (g_E.cursorY < g_E.rowOffset) {
        g_E.rowOffset = g_E.cursorY;
    }
    if (g_E.cursorY >= g_E.rowOffset + g_E.screenRows) {
        g_E.rowOffset = g_E.cursorY - g_E.screenRows + 1;
    }
}

constexpr char ctrlWith(char c) { return (c & 0x1f); }

void moveCursor(int key) {
    std::string currentLine =
        (g_E.cursorY >= g_E.lines.size()) ? "" : g_E.lines[g_E.cursorY];
    switch (key) {
        case ARROW_LEFT:
            if (g_E.cursorX > 0) {  // not first character in current line
                g_E.cursorX--;
            } else if (g_E.cursorY > 0) {  // not first line
                // move cursor to the end of previous line
                g_E.cursorY--;
                g_E.cursorX = g_E.lines[g_E.cursorY].size();
            }
            break;
        case ARROW_RIGHT:
            // limit cursor to the end of current line
            if (currentLine.size() > 0 && g_E.cursorX < currentLine.size())
                g_E.cursorX++;
            else if (g_E.cursorY < g_E.lines.size() &&
                     g_E.cursorX == currentLine.size()) {
                // move cursor to the beginning of next line
                g_E.cursorY++;
                g_E.cursorX = 0;
            }
            break;
        case ARROW_UP:
            if (g_E.cursorY > 0) g_E.cursorY--;
            break;
        case ARROW_DOWN:
            if (g_E.cursorY < g_E.lines.size()) g_E.cursorY++;
            break;
    }
    // snap back to the end of line if curosr is moved to the past of line
    currentLine =
        (g_E.cursorY >= g_E.lines.size()) ? "" : g_E.lines[g_E.cursorY];
    int rowLen = currentLine.size() ? currentLine.size() : 0;
    if (g_E.cursorX > rowLen) g_E.cursorX = rowLen;
}

void save() {
    if (g_E.filename.size() == 0) return;

    std::string out;
    for (auto& line : g_E.lines) {
        out += line + "\n";
    }

    std::ofstream ofs(g_E.filename);
    ofs << out;
}

void deleteChar() {
    if (g_E.cursorY == g_E.lines.size()) return;
    if (g_E.cursorY == 0 && g_E.cursorX == 0) return;
    if (g_E.cursorX > 0) {
        int xpos = g_E.cursorX;
        const std::string& org = g_E.lines[g_E.cursorY];
        g_E.lines[g_E.cursorY] =
            org.substr(0, xpos - 1) + org.substr(xpos, org.size() - (xpos));
        g_E.renders[g_E.cursorY] =
            convertToRenderingRow(g_E.lines[g_E.cursorY]);
        g_E.cursorX--;
    } else {  // back space at the start of line
        g_E.cursorX = g_E.lines[g_E.cursorY - 1].size();
        g_E.lines[g_E.cursorY - 1] += g_E.lines[g_E.cursorY];
        g_E.renders[g_E.cursorY - 1] += g_E.renders[g_E.cursorY];
        g_E.lines.erase(g_E.lines.begin() + g_E.cursorY);
        g_E.renders.erase(g_E.renders.begin() + g_E.cursorY);
        g_E.cursorY--;
    }
    g_E.modified = true;
}

void insertLine() {
    int ypos = g_E.cursorY;
    g_E.lines.insert(g_E.lines.begin() + ypos + 1, "");
    g_E.renders.insert(g_E.renders.begin() + ypos + 1, "");
    int xpos = g_E.cursorX;
    g_E.renders[ypos + 1] =
        g_E.renders[ypos].substr(xpos, g_E.renders[ypos].size() - xpos);
    g_E.renders[ypos] = g_E.renders[ypos].substr(0, xpos);
    g_E.lines[ypos + 1] =
        g_E.lines[ypos].substr(xpos, g_E.lines[ypos].size() - xpos);
    g_E.lines[ypos] = g_E.lines[ypos].substr(0, xpos);
    g_E.cursorY++;
    g_E.cursorX = 0;
}

void processKey(int c) {
    if (c == 0) return;  // no input
    switch (c) {
        case '\r':  // enter key
            insertLine();
            break;

        case ctrlWith('q'): {
            std::string buf;
            clearScreen(buf);
            writeTerminal(buf.c_str(), buf.size());
            exit(0);
            break;
        }

        case ctrlWith('s'): {
            save();
            break;
        }

        case HOME_KEY:
            g_E.cursorX = 0;
            break;
        case END_KEY:
            if (g_E.cursorY < g_E.lines.size())
                g_E.cursorX = g_E.lines[g_E.cursorY].size();
            break;

        case BACKSPACE:
        case ctrlWith('h'):
        case DEL_KEY:
            if (c == DEL_KEY) moveCursor(ARROW_RIGHT);
            deleteChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN: {
            if (c == PAGE_UP) {
                // set cursor position to top of screen
                g_E.cursorY = g_E.rowOffset;
            } else if (c == PAGE_DOWN) {
                // set curosr position to bottom of screen
                g_E.cursorY = g_E.rowOffset + g_E.screenRows - 1;
                if (g_E.cursorY > g_E.lines.size())
                    g_E.cursorY = g_E.lines.size();
            }
            // go up number of screen row times
            int times = g_E.screenRows;
            while (times--) moveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            break;
        }

        case ARROW_DOWN:
        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            moveCursor(c);
            break;

        case ctrlWith('l'):  // refresh key in traditional terminal app
        case '\x1b':         // escape key
            break;

        default: {
            if (g_E.cursorY == g_E.lines.size()) {
                std::string toBeAdd = "";
                g_E.lines.emplace_back(toBeAdd);
                g_E.renders.emplace_back(convertToRenderingRow(toBeAdd));
            }
            int y = g_E.cursorY;
            g_E.lines[y].insert(g_E.cursorX, 1, c);
            g_E.renders[y] = convertToRenderingRow(g_E.lines[y]);
            g_E.cursorX++;
            break;
        }
    }
}

void drawRows(std::string& buf) {
    for (int y = 0; y < g_E.screenRows; ++y) {
        int filerow = y + g_E.rowOffset;
        if (filerow >= g_E.lines.size()) {
            if ((g_E.lines.size() == 0) && (y == g_E.screenRows / 3)) {
                char welcome[80];
                int wellen = snprintf(welcome, sizeof(welcome),
                                      "cp editor -- version %s", "0.0.1");
                if (wellen > g_E.screenCols) wellen = g_E.screenCols;
                int padding = (g_E.screenCols - wellen) / 2;
                if (padding) {
                    buf += "~";
                    padding--;
                }
                while (padding--) buf += " ";
                buf += welcome;
            } else {
                // draw left side tilde
                buf += "~";
            }
        } else {
            int len = g_E.renders[filerow].size();
            if (len > g_E.screenCols) len = g_E.screenCols;
            const std::string& toBeAdd = g_E.renders[filerow];
            if (0 < toBeAdd.size() && (g_E.colOffset < toBeAdd.size()))
                buf += toBeAdd.substr(g_E.colOffset);
        }

        buf += "\x1b[K";  // clear one line
        // if (y < g_E.screenRows - 1)
        buf += "\r\n";
    }
}

void drawStatusBar(std::string& buf, int currentC) {
    buf += "\x1b[7m";  // switch color mode to inverted

    char status[80], rstatus[80];
    int len =
        snprintf(status, sizeof(status),
                 "Filename: %.20s - %d lines, key pressed: %c(%d)",
                 g_E.filename.size() > 0 ? g_E.filename.c_str() : "[No Name]",
                 static_cast<int>(g_E.lines.size()),
                 static_cast<char>(currentC), currentC);
    if (len > g_E.screenCols) len = g_E.screenCols;
    buf += std::string(status);

    int rlen = snprintf(rstatus, sizeof(rstatus), "CursorPosition Y : %d/%d",
                        g_E.cursorY + 1, static_cast<int>(g_E.lines.size()));

    while (len < g_E.screenCols) {
        if (g_E.screenCols - len == rlen) {
            // append right-side status
            buf += std::string(rstatus);
            break;
        }
        buf += " ";
        len++;
    }
    buf += "\x1b[m";  // switch back to color mode normal
    buf += "\r\n";
}

void drawMessageBar(std::string& buf) {
    buf += "\x1b[K";  // clear one line
    int len = g_E.statusMsg.size();
    if (len > g_E.screenCols) len = g_E.screenCols;
    if (len && (time(nullptr) - g_E.statusMsgTime < 5)) {
        buf += g_E.statusMsg.substr(0, len);
    }
}

void refreshScreen(int currentC) {
    editorScroll();

    std::string buf;
    buf += "\x1b[?25l";  // hide cursor (l is reset command)
    buf += "\x1b[H";     // H command to position the cursor
    drawRows(buf);
    drawStatusBar(buf, currentC);
    drawMessageBar(buf);

    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "\x1b[%d;%dH",
             (g_E.cursorY - g_E.rowOffset) + 1,
             (g_E.cursorRX - g_E.colOffset) + 1);
    buf += std::string(cbuf);

    buf += "\x1b[?25h";  // show cursor again (h is set command)

    writeTerminal(buf.c_str(), buf.size());
}

void setStatusMessage(const std::string& msg) {
    g_E.statusMsg = msg;
    g_E.statusMsgTime = time(nullptr);
}

/*** init ***/
int main(int argc, const char* argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    setStatusMessage("Help: Ctrl-q = quit");

    while (true) {
        int c = readKey();
        refreshScreen(c);
        processKey(c);
    }

    return 0;
}
