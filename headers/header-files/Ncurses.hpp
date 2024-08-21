#ifndef _NCURSES
#define _NCURSES

#include <ncurses.h>
#include <pthread.h>
#include <thread>
#include "HandleClient.hpp"

pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;

class Ncurses
{
public:
    static void threadSafeWrefresh(WINDOW *win)
    {
        pthread_mutex_lock(&ncurses_mutex);
        wrefresh(win);
        pthread_mutex_unlock(&ncurses_mutex);
    }
    static void startUserMenu(WINDOW *msg_input_win, WINDOW *subwin, WINDOW *msg_view_win, SSL *tlsSock, const std::string &userStr, EVP_PKEY *receivedPublicKey, EVP_PKEY *privateKey)
    {
        initscr();
        cbreak();
        nonl();
        noecho();
        keypad(stdscr, TRUE);

        int height = LINES;
        int width = COLS;

        int msg_view_h = height - 3;
        int msg_input_h = 3;

        msg_input_win = newwin(msg_input_h, width, msg_view_h, 0);
        box(msg_input_win, 0, 0);

        msg_view_win = newwin(msg_view_h - 1, width - 2, 1, 1);
        box(msg_view_win, 0, 0);

        threadSafeWrefresh(msg_view_win);
        threadSafeWrefresh(msg_input_win);

        mvwprintw(msg_view_win, 0, 4, "Chat");
        threadSafeWrefresh(msg_view_win);

        subwin = derwin(msg_view_win, height - 6, width - 4, 1, 1);
        scrollok(subwin, TRUE);
        idlok(subwin, TRUE);

        wmove(msg_input_win, 1, 1);

        std::thread(handleClient::receiveMessages, tlsSock, subwin, privateKey, receivedPublicKey).detach();
        std::thread(handleClient::handleInput, std::ref(userStr), receivedPublicKey, tlsSock, subwin, msg_input_win).join();
    }
};

#endif