#ifndef MAIN_H
#define MAIN_H

#include <X11/Xlib.h>

int on_xerror(Display *d, XErrorEvent *e);
int on_xerror_detect(Display *d, XErrorEvent *e);
void on_sigchld(int s);

#endif /* MAIN_H */
