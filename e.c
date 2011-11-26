/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <locale.h>
#include <signal.h>
#include <ncurses.h>

typedef struct Node Node;
typedef struct List List;

struct Node {
  Node *next;
  Node *prev;
  void *data;
};

struct List {
  Node *head;
  Node *tail;
  int size;
};

#define FOR_EACH_NODE(list, node_p) \
  for(node_p=(list).head; node_p; node_p=node_p->next)

/* Create node <new> that points to <data> and insert 
  this node into list.
  If <after>==NULL, then <new> will be added at the head
  of the list, else it will be added following <after>.
  Only pointer to data is stored, no copying! */
void
insert_node (List *list, void *data, Node *after){
  Node *new = malloc(sizeof(Node));
  new->data = data;
  if(after){
    new->next = after->next;
    new->prev = after;
    after->next = new;
  }else{
    new->next = list->head;
    new->prev = after;
    list->head = new;
  }
  if(new->next)
    new->next->prev = new;
  else
    list->tail = new;
  list->size++;
}

/* Extructs node from list, returns pointer to this node.
  No memory is freed */
Node *
extruct_node (List *list, Node *nd){
  if(!nd)
    return(NULL);
  if(nd->next)
    nd->next->prev = nd->prev;
  else
    list->tail = nd->prev;
  if(nd->prev)
    nd->prev->next = nd->next;
  else
    list->head = nd->next;
  list->size--;
  return(nd);
}

/* Delete data and node. */
void
delete_node (List *list, Node *nd){
  Node *tmp = extruct_node(list, nd);
  free(tmp->data);
  free(tmp);
}

/* Extruct node from list, delete node,
  return pointer to data. */
void *
extruct_data (List *list, Node *old){
  Node *node = extruct_node(list, old);
  void *data = node->data;
  free(node);
  return(data);
}

void
add_node_to_head(List *list, void *data){
  insert_node(list, data, NULL);
}

void
add_mode_after(List *list, void *data, Node *after){
  insert_node(list, data, after);
}

void
add_node_to_tail(List *list, void *data){
  insert_node(list, data, list->tail);
}

typedef struct { int y; int x; } Pos;

/*list of 'char*'*/
typedef List Buffer;

typedef struct {
  int first;
  int last_in_original;
  int last_in_changed;
  Buffer from_original;
  Buffer from_changed;
} Diff;

/*list of 'Diff'*/
typedef List Diff_stack;

typedef struct {
  Diff_stack undo; 
  Diff_stack redo;
  Buffer lines;
  Buffer prevlines;
  Pos cursor;
  Pos marker;
  Pos screen_pos;
} Win;

bool is_running = true;
Buffer clipboard = {NULL, NULL, 0};
Pos screen_size = {0, 0};
char search_template[100] = "...";
char statusline[200] = "ozkriff's ed";
char filename[100];
List windows = {NULL, NULL, 0};
Win *win = NULL;

bool
is_ascii(char c){
  unsigned char uc = c;
  return(uc < 0x80);
}

bool
is_fill(char c){
  unsigned char uc = c;
  return(!is_ascii(c) && uc <= 0xBF);
}

int
utf8len(char ch){
  unsigned char uc = (unsigned char)ch;
  if(uc >= 0xFC)
    return(6);
  else if(uc >= 0xF8)
    return(5);
  else if(uc >= 0xF0)
    return(4);
  else if(uc >= 0xE0)
    return(3);
  else if(uc >= 0xC0)
    return(2);
  else
    return(1);
}

char *
my_strdup(const char *s){
  char *d = calloc(strlen(s)+1, sizeof(char));
  if(d != NULL)
    strcpy(d, s);
  return(d);
}
 
Node *
id2node(Buffer b, int line){
  Node *nd = b.head;
  int i = 0;
  FOR_EACH_NODE(b, nd){
    if(i == line)
      return(nd);
    i++;
  }
  return(NULL);
}

char *
id2str(Buffer b, int line){
  char *s = id2node(b, line)->data;
  return(s);
}

Buffer
copy(Buffer original, int from, int to){
  Buffer b = {NULL, NULL, 0};
  Node *n = id2node(original, from);
  while(from <= to && n){
    char *s = n->data;
    add_node_to_tail(&b, my_strdup(s));
    n = n->next;
    from++;
  }
  return(b);
}

void
paste(Buffer *to, Buffer from, int fromline){
  Node *n = id2node(*to, fromline);
  int i = fromline;
  FOR_EACH_NODE(from, n){
    char *s = n->data;
    insert_node(to, my_strdup(s), id2node(*to, i));
    i++;
  }
}

void
removelines(Buffer *b, int from, int count){
  while(count > 0){
    delete_node(b, id2node(*b, from));
    count--;
  }
}

Buffer
clone_buffer(Buffer buffer){
  Buffer newlist = {NULL, NULL, 0};
  Node *n;
  FOR_EACH_NODE(buffer, n){
    char *s = n->data;
    add_node_to_tail(&newlist, my_strdup(s));
  }
  return(newlist);
}

void
clear_buffer(Buffer *buffer){
  while(buffer->size > 0)
    delete_node(buffer, buffer->head);
}

void
clean_stack(List *stack){
  while(stack->size > 0){
    Diff *d = stack->tail->data;
    clear_buffer(&d->from_original);
    clear_buffer(&d->from_original);
    delete_node(stack, stack->tail);
  }
}

int
find_first_changed_line(Buffer changed, Buffer original){
  int id = 0;
  Node *n1 = changed.head;
  Node *n2 = original.head;
  while(n1 && n2){
    if(strcmp(n1->data, n2->data) != 0)
      return(id);
    n1 = n1->next;
    n2 = n2->next;
    id++;
  }
  return(changed.size - 1);
}

int
find_last_changed_line(Buffer changed, Buffer original){
  int id = original.size - 1;
  Node *n1 = changed.tail;
  Node *n2 = original.tail;
  while(n1 && n2){
    if(strcmp(n1->data, n2->data) != 0)
      return(id);
    n1 = n1->prev;
    n2 = n2->prev;
    id--;
  }
  return(0);
}

int
find_last_new_line(Buffer changed, Buffer original){
  int id = changed.size - 1;
  Node *n1 = changed.tail;
  Node *n2 = original.tail;
  while(n1 && n2){
    if(strcmp(n1->data, n2->data) != 0)
      return(id);
    n1 = n1->prev;
    n2 = n2->prev;
    id--;
  }
  return(0);
}

Diff
create_diff(Buffer changed, Buffer original){
  Diff d;
  d.first = find_first_changed_line(changed, original);
  d.last_in_original = find_last_changed_line(changed, original);
  d.last_in_changed = find_last_new_line(changed, original);
  d.from_original = copy(original, d.first, d.last_in_original);
  d.from_changed = copy(changed, d.first, d.last_in_changed);
  return(d);
}

void
undo_diff(Buffer *buffer, Diff d){
  removelines(buffer, d.first, d.from_changed.size);
  paste(buffer, d.from_original, d.first - 1);
}

void
redo_diff(Buffer *buffer, Diff d){
  removelines(buffer, d.first, d.from_original.size);
  paste(buffer, d.from_changed, d.first - 1);
}

void
add_undo_copy(){
  Diff *d = calloc(1, sizeof(Diff));
  *d = create_diff(win->lines, win->prevlines);
  add_node_to_tail(&win->undo, d);
  redo_diff(&win->prevlines, *d);
  clean_stack(&win->redo);
}

void
move_last_diff(Diff_stack *st1, Diff_stack *st2){
  if(st1->size > 0){
    Diff *d = extruct_data(st1, st1->tail);
    add_node_to_tail(st2, d);
  }
}

void
undo(){
  if(win->undo.size > 0){
    Diff *d = win->undo.tail->data;
    undo_diff(&win->lines, *d);
    move_last_diff(&win->undo, &win->redo);
  }
}

void
redo(){
  if(win->redo.size > 0){
    Diff *d = win->redo.tail->data;
    redo_diff(&win->lines, *d);
    move_last_diff(&win->redo, &win->undo);
  }
}

void
readfile(Buffer *b, char *filename){
  FILE *f = fopen(filename, "r");
  char s[300];
  while(fgets(s, 300, f))
    add_node_to_tail(b, my_strdup(s));
  fclose(f);
  sprintf(statusline, "opened '%s'", filename);
}

void
clear_statusline(){
  move(screen_size.y, 0);
  clrtoeol();
}

bool
really(char *message){
  char c;
  clear_statusline();
  echo();
  move(screen_size.y, 0);
  printw(message);
  c = getch();
  noecho();
  return(c == 'y');
}

void
writefile(Buffer b, char *filename){
  Node *nd;
  FILE *f = fopen(filename, "w");
  if(!really("Save file? [y/n]"))
    return;
  if(!f){
    puts("NO FILE!");
    exit(1);
  }
  FOR_EACH_NODE(b, nd){
    char *s = nd->data;
    fputs(s, f);
  }
  fclose(f);
  sprintf(statusline, "written %s", filename);
}

void
drawline(char *s){
  int len = strlen(s);
  addnstr(s, screen_size.x-1);
  if(len > screen_size.x-1)
    addch('\n');
}

void
drawlines(Buffer b, int from, int count){
  Node *nd = id2node(b, from);
  while(nd && count > 0){
    char *s = nd->data;
    drawline(s);
    nd = nd->next;
    count--;
  }
}

void
draw_statusline(){
  char s[120];
  sprintf(s, "(c-%i-%i  m-%i-%i  u-%i/%i)  %s",
      win->cursor.y, win->cursor.x,
      win->marker.y, win->marker.x,
      win->undo.size,
      win->redo.size,
      statusline);
  move(screen_size.y, 0);
  clear_statusline();
  printw(s);
}

int
find_screen_x(Pos p){
  /*offset in screen positions*/
  int screen_x = 0;
  /*offset in bytes*/
  int o = 0;
  char *s = id2str(win->lines, p.y);
  while(o < p.x){
    if(s[o] == '\t')
      screen_x += TABSIZE;
    else
      screen_x += 1;
    o += utf8len(s[o]);
  }
  return(screen_x);
}

void
draw(){
  move(0, 0);
  drawlines(win->lines, win->screen_pos.y, screen_size.y);
  draw_statusline();
  move(win->cursor.y - win->screen_pos.y, find_screen_x(win->cursor));
  refresh();
}

void
insert_text_into_line(char *s, int len, Pos p){
  char *old_s = id2str(win->lines, p.y);
  char *new_s = calloc(strlen(old_s) + 1 + len, sizeof(char));
  strncpy(new_s, old_s, p.x);
  strncpy(new_s + p.x, s, len);
  strcpy(new_s + p.x + len, old_s + p.x);
  free(old_s);
  id2node(win->lines, p.y)->data = new_s;
}

void
get_utf8char(char c[6], int *len){
  int i;
  c[0] = getch();
  (*len) = utf8len(c[0]);
  for(i = 1; i < (*len); i++)
    c[i] = getch();
}

void
move_nextln(){
  char *s;
  int n;
  if(win->cursor.y == (win->lines.size-1))
    return;
  win->cursor.y++;
  s = id2str(win->lines, win->cursor.y);
  n = strlen(s)-1;
  if(win->cursor.x > n)
    win->cursor.x = n;
}

void
newstr(char *data){
  insert_node(&win->lines, my_strdup(data), id2node(win->lines, win->cursor.y));
  move_nextln();
  win->cursor.x = 0;
}

void
insert_char(char c[6], int len, Pos p){
  if(c[0] == '\n'){
    char *s = id2str(win->lines, p.y);
    int old_x = p.x;
    newstr(s + p.x);
    s[old_x] = '\n';
    s[old_x + 1] = '\0';
  }else{
    insert_text_into_line(c, len, p);
  }
}

void
insert(){
  char c[6];
  int len;
  sprintf(statusline, "insert mode. ESC - return to normal mode");
  draw();
  while(1){
    get_utf8char(c, &len);
    if(c[0] == 0x1B/*esc*/){
      sprintf(statusline, "normal mode");
      return;
    }
    insert_char(c, len, win->cursor);
    if(c[0] != '\n')
      win->cursor.x += len;
    draw();
  }
}

void
move_prevln(){
  char *s;
  int n;
  if(win->cursor.y == 0)
    return;
  win->cursor.y--;
  s = id2str(win->lines, win->cursor.y);
  n = strlen(s)-1;
  if(win->cursor.x > n)
    win->cursor.x = n;
}

void
move_nextch(){
  char *s = id2str(win->lines, win->cursor.y);
  win->cursor.x += utf8len(s[win->cursor.x]);
  if(s[win->cursor.x] == '\0'){
    move_nextln();
    win->cursor.x = 0;
  }
}

int
find_prev_char_offset(Pos p){
  unsigned char c;
  char *s = id2str(win->lines, p.y);
  int of = p.x; /*offset in bytes*/
  do{
    of--;
    c = s[of];
  } while(of >= 0 && is_fill(c));
  return(of);
}

void
move_prevch(){
  char *s;
  if(win->cursor.x == 0){
    move_prevln();
    s = id2str(win->lines, win->cursor.y);
    win->cursor.x = strlen(s)-1;
  }else{
    win->cursor.x = find_prev_char_offset(win->cursor);
  }
}

void
join(){
  char *s_orig = id2str(win->lines, win->cursor.y);
  char *s_next = id2str(win->lines, win->cursor.y + 1);
  int len_orig = strlen(s_orig) + 1;
  int len_next = strlen(s_next) + 1;
  char *s_new = malloc(len_orig + len_next);
  strcpy(s_new, s_orig);
  strcpy(s_new + len_orig - 2, s_next);
  delete_node(&win->lines, id2node(win->lines, win->cursor.y + 1));
  id2node(win->lines, win->cursor.y)->data = s_new;
  free(s_orig);
}

void
removechar(){
  char *s = id2str(win->lines, win->cursor.y);
  if(s[win->cursor.x] == '\n')
    join();
  else
    strcpy(s + win->cursor.x, s + win->cursor.x + utf8len(s[win->cursor.x]));
}

void
replace_char(){
  char c[6];
  int len; /*character size in bytes*/
  removechar();
  get_utf8char(c, &len);
  insert_char(c, len, win->cursor);
}

void
move_halfscreenup(){
  int i;
  for(i=0; i<screen_size.y/2; i++)
    move_prevln();
}

void
move_halfscreendown(){
  int i;
  for(i=0; i<screen_size.y/2; i++)
    move_nextln();
}

void
move_toline(){
  int n;
  move(screen_size.y, 0);
  printw("enter line number: ");
  echo();
  scanw("%i", &n);
  noecho();
  if(n < 0 || n > win->lines.size - 1){
    sprintf(statusline, "bad line number");
  }else{
    sprintf(statusline, "moved to %i line", n);
    win->cursor.y = n;
  }
}

void
correct_scr(){
  while(win->cursor.y < win->screen_pos.y)
    win->screen_pos.y--;
  while(win->cursor.y >= win->screen_pos.y + screen_size.y)
    win->screen_pos.y++;
}

/* get offset of substring */
int
get_offset(char *s, char *search_template){
  int o; /* offset */
  char *p, *p2;
  for(o=0; s[o]; o++) {
    p = &s[o];
    p2 = search_template;
    while(*p2 && *p2==*p) {
      p++;
      p2++;
    }
    if(!*p2)
      return(o);
  }
  return(-1);
}

void
findnext(){
  Node *nd;
  int y = win->cursor.y + 1;
  if(y >= win->lines.size)
    y = 0;
  nd = id2node(win->lines, y);
  while(nd && y < win->lines.size){
    char *s = nd->data;
    if(strstr(s, search_template)){
      win->cursor.y = y;
      win->cursor.x = get_offset(s, search_template);
      return;
    }
    nd = nd->next;
    y++;
  }
}

void
get_search_template(){
  move(screen_size.y, 0);
  echo();
  printw("enter template: ");
  scanw("%s", search_template);
  noecho();
  findnext();
}

void
writeas(Buffer b){
  char newfname[100]; /* new file name */
  echo();
  move(screen_size.y, 0);
  printw("Enter new file name: ");
  scanw("%s", newfname);
  noecho();
  writefile(b, newfname);
}

void
setmark(){
  win->marker = win->cursor;
}

void
correct_x(){
  int len = strlen(id2str(win->lines, win->cursor.y));
  if(win->cursor.x >= len)
    win->cursor.x = len;
  if(win->cursor.x < 0)
    win->cursor.x = 0;
}

void
quit(){
  if(really("quit? (y/n)"))
    is_running = false;
}

/*Move cursor to beginig of line*/
void
move_bol(){
  win->cursor.x = 0;
}

/*Move cursor to ending of buffer*/
void
move_eol(){
  win->cursor.x = strlen(id2str(win->lines, win->cursor.y))-1;
}

/*Move cursor to beginig of buffer*/
void
move_bob(){
  win->cursor.y = 0;
}

/*Move cursor to ending of buffer*/
void
move_eob(){
  win->cursor.y = win->lines.size-1;
}

void
copy_to_clipboard(){
  clear_buffer(&clipboard);
  clipboard = copy(win->lines, win->marker.y, win->cursor.y);
}

void
removeselected(){
  removelines(&win->lines, win->marker.y, 1 + win->cursor.y - win->marker.y);
  win->cursor.y = win->marker.y;
}

void
insert_empty_line(){
  newstr("\n");
}

void
next_win(){
  Node *node = windows.head;
  while(node->data != win && node){
    node = node->next;
  }
  if(node->next)
    win = node->next->data;
  else
    win = windows.head->data;
  clear();
}

void
command(char c){
  if(c=='q') quit();
  else if(c=='h') move_prevch();
  else if(c=='l') move_nextch();
  else if(c=='j') move_nextln();
  else if(c=='k') move_prevln();
  else if(c=='H') move_bol();
  else if(c=='L') move_eol();
  else if(c=='d') move_halfscreendown();
  else if(c=='u') move_halfscreenup();
  else if(c=='D') move_eob();
  else if(c=='U') move_bob();
  else if(c=='g') move_toline();
  else if(c=='F') get_search_template();
  else if(c=='f') findnext();
  else if(c=='w') writefile(win->lines, filename);
  else if(c=='W') writeas(win->lines);
  else if(c=='m') setmark();
  else if(c=='c') copy_to_clipboard();
  else if(c=='o') { insert_empty_line(); add_undo_copy(); }
  else if(c=='i') { insert(); add_undo_copy(); }
  else if(c=='r') { replace_char(); add_undo_copy(); }
  else if(c=='x') { removechar(); add_undo_copy(); }
  else if(c=='X') { removeselected(); add_undo_copy(); }
  else if(c=='p') { paste(&win->lines, clipboard, win->cursor.y); add_undo_copy(); }
  else if(c=='[') undo();
  else if(c==']') redo();
  else if(c=='n') next_win();
}

void
mainloop(){
  char c;
  while(is_running){
    c = (char)getch();
    sprintf(statusline, "key '%i'", (int)c);
    command(c);
    correct_scr();
    correct_x();
    draw();
  }
}

/*The easiest way to handle SIGWINCH is
  to do an endwin, followed by an refresh
  and a screen repaint you code  yourself.
  The refresh will pick up the new screen
  size from the xterm's environment. */
void
handle_resize(){
  endwin();
  refresh();
  getmaxyx(stdscr, screen_size.y, screen_size.x);
  screen_size.y--;
  draw();
}

void
init(){
  signal(SIGWINCH, handle_resize);
  setlocale(LC_ALL,"");
  initscr();
  noecho();
  getmaxyx(stdscr, screen_size.y, screen_size.x);
  screen_size.y--;
  TABSIZE = 4;
}

void
create_empty_buffer(){
  newstr("");
}

void
create_win(char *filename){
  Win *w = calloc(1, sizeof(Win));
  add_node_to_tail(&windows, w);
  win = w;
  if(filename)
    readfile(&w->lines, filename);
  else
    create_empty_buffer();
  w->prevlines = clone_buffer(w->lines);
}

void
arg_proc(int ac, char **av){
  int i;
  if(ac > 1){
    for(i = 1; i < ac; i++)
      create_win(av[i]);
  }else{
    create_win(NULL);
  }
}

int
main(int ac, char **av){
  init();
  arg_proc(ac, av);
  draw();
  mainloop();
  clear();
  endwin();
  return(0);
}

