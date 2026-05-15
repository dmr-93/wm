#ifndef MENU_H
#define MENU_H

void spawn(const char *cmd);
void show_menu(int mx, int my);
void taskbar_click(int lx);
void load_menu_config(void);
void free_menu_config(void);

#endif /* MENU_H */
