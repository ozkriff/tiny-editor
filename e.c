/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <locale.h>
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

bool is_running = true;
List undo_stack = {NULL, NULL, 0}; 
List redo_stack = {NULL, NULL, 0};
Buffer lines = {NULL, NULL, 0};
Buffer clipboard = {NULL, NULL, 0};
Pos cursor = {0, 0};
Pos marker = {0, 0};
Pos scrpos = {0, 0};
Pos screen_size = {0, 0};
char search_template[100] = "...";
char statusline[200] = "[ozkriff's ed]";
char filename[100];

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
    Buffer *buffer = stack->tail->data;
    clear_buffer(buffer);
    delete_node(stack, stack->tail);
  }
}

void
add_undo_copy(){
  Buffer *new_buffer = calloc(1, sizeof(Buffer));
  *new_buffer = clone_buffer(lines);
  add_node_to_tail(&undo_stack, new_buffer);
  clean_stack(&redo_stack);
}

/* move last buffer from stack1 to stack2 */
void
move_last_buffer(List *st1, List *st2){
  if(st1->size > 0){
    Buffer *buffer = extruct_data(st1, st1->tail);
    add_node_to_tail(st2, buffer);
  }
}

void
undo(){
  if(undo_stack.size > 0){
    Buffer *buffer;
    clear_buffer(&lines);
    buffer = undo_stack.tail->data;
    lines = clone_buffer(*buffer);
    move_last_buffer(&undo_stack, &redo_stack);
  }
}

void
redo(){
  if(redo_stack.size > 0){
    Buffer *buffer;
    clear_buffer(&lines);
    buffer = redo_stack.tail->data;
    lines = clone_buffer(*buffer);
    move_last_buffer(&redo_stack, &undo_stack);
  }
}

void
readfile(char *filename){
  FILE *f = fopen(filename, "r");
  char s[300];
  while(fgets(s, 300, f))
    add_node_to_tail(&lines, my_strdup(s));
  fclose(f);
  sprintf(statusline, "[opened '%s']", filename);
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
writefile(char *filename){
  Node *nd;
  FILE *f = fopen(filename, "w");
  if(!really("Save file? [y/n]"))
    return;
  if(!f){
    puts("NO FILE!");
    exit(1);
  }
  FOR_EACH_NODE(lines, nd){
    char *s = nd->data;
    fputs(s, f);
  }
  fclose(f);
  sprintf(statusline, "[written %s]", filename);
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
      cursor.y, cursor.x,
      marker.y, marker.x,
      undo_stack.size,
      redo_stack.size,
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
  char *s = id2str(lines, p.y);
  while(o < p.x){
    screen_x++;
    o += utf8len(s[o]);
  }
  return(screen_x);
}

void
draw(){
  move(0, 0);
  drawlines(lines, scrpos.y, screen_size.y);
  draw_statusline();
  move(cursor.y-scrpos.y, find_screen_x(cursor));
  refresh();
}

void
mv_nextln(){
  char *s;
  int n;
  if(cursor.y == (lines.size-1))
    return;
  cursor.y++;
  s = id2str(lines, cursor.y);
  n = strlen(s)-1;
  if(cursor.x > n)
    cursor.x = n;
}

void
mv_prevln(){
  char *s;
  int n;
  if(cursor.y == 0)
    return;
  cursor.y--;
  s = id2str(lines, cursor.y);
  n = strlen(s)-1;
  if(cursor.x > n)
    cursor.x = n;
}

void
mv_nextch(){
  char *s = id2str(lines, cursor.y);
  cursor.x += utf8len(s[cursor.x]);
  if(s[cursor.x] == '\0'){
    mv_nextln();
    cursor.x = 0;
  }
}

int
find_prev_char_offset(Pos p){
  unsigned char c;
  char *s = id2str(lines, p.y);
  int of = p.x; /*offset in bytes*/
  do{
    of--;
    c = s[of];
  } while(of >= 0 && is_fill(c));
  return(of);
}

void
mv_prevch(){
  char *s;
  if(cursor.x == 0){
    mv_prevln();
    s = id2str(lines, cursor.y);
    cursor.x = strlen(s)-1;
  }else{
    cursor.x = find_prev_char_offset(cursor);
  }
}

void
newstr(char *data){
  insert_node(&lines, my_strdup(data), id2node(lines, cursor.y));
  mv_nextln();
  cursor.x = 0;
}

void
replace_char(char c){
  char *s = id2str(lines, cursor.y);
  if(c=='\n'){
    int oldx = cursor.x;
    newstr(s + cursor.x);
    s[oldx] = '\n';
    s[oldx+1] = '\0';
  }else if(c=='\t'){
    s[cursor.x] = ' ';
  }else{
    s[cursor.x] = c;
  }
}

void
insert(){
  char c;
  char *str;
  char *nstr;
  sprintf(statusline, "[insert mode. ESC - return to normal mode]");
  draw();
  while( (c=getch()) != 27){
    if(c!='\n'){
      str = id2str(lines, cursor.y);
      nstr = calloc(strlen(str)+1+1, sizeof(char));
      strncpy(nstr, str, cursor.x);
      strcpy(nstr + cursor.x + 1, str + cursor.x);
      free(str);
      id2node(lines, cursor.y)->data = nstr;
      replace_char(c);
      cursor.x++;
    }else{
      replace_char(c);
    }
    draw();
  }
  sprintf(statusline, "[normal mode]");
}

void
screenup(){
  int i;
  for(i=0; i<screen_size.y/2; i++)
    mv_prevln();
}

void
screendown(){
  int i;
  for(i=0; i<screen_size.y/2; i++)
    mv_nextln();
}

void
join(char *s){
  int len1 = strlen(s) + 1;
  /* next string */
  char *s2 = id2str(lines, cursor.y+1);
  int len2 = strlen(s2) + 1;
  /* new string */
  char *ns = malloc(len1 + len2);
  strcpy(ns, s);
  strcpy(ns + len1 - 2, s2);
  delete_node(&lines, id2node(lines, cursor.y+1));
  id2node(lines, cursor.y)->data = ns;
  free(s);
}

void
removechar(){
  char *s = id2str(lines, cursor.y);
  if(s[cursor.x] == '\n')
    join(s);
  else
    strcpy(s+cursor.x, s+cursor.x+utf8len(s[cursor.x]));
}

void
gotostr(){
  int n;
  move(screen_size.y, 0);
  printw("enter line number: ");
  echo();
  scanw("%i", &n);
  noecho();
  if(n < 0 || n > lines.size - 1){
    sprintf(statusline, "[bad line number]");
  }else{
    sprintf(statusline, "[moved to %i line]", n);
    cursor.y = n;
  }
}

void
correct_scr(){
  while(cursor.y < scrpos.y)
    scrpos.y--;
  while(cursor.y >= scrpos.y+screen_size.y)
    scrpos.y++;
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
  int y = cursor.y + 1;
  if(y >= lines.size)
    y = 0;
  nd = id2node(lines, y);
  while(nd && y < lines.size){
    char *s = nd->data;
    if(strstr(s, search_template)){
      cursor.y = y;
      cursor.x = get_offset(s, search_template);
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
removelines(Buffer *b, int from, int count){
  while(count > 0){
    delete_node(b, id2node(*b, from));
    count--;
  }
}

void
writeas(){
  char newfname[100]; /* new file name */
  echo();
  move(screen_size.y, 0);
  printw("Enter new file name: ");
  scanw("%s", newfname);
  noecho();
  writefile(newfname);
}

void
setmark(){
  marker = cursor;
}

void
clean_clipboard(){
  while(clipboard.size != 0)
    delete_node(&clipboard, clipboard.head);
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
  int i = fromline - 1;
  FOR_EACH_NODE(from, n){
    char *s = n->data;
    insert_node(to, my_strdup(s), id2node(*to, i));
    i++;
  }
}

void
correct_x(){
  int len = strlen(id2str(lines, cursor.y));
  if(cursor.x >= len)
    cursor.x = len;
  if(cursor.x < 0)
    cursor.x = 0;
}

void
quit(){
  if(really("quit? (y/n)"))
    is_running = false;
}

void
mainloop(){
  int c;
  while(is_running){
    c = getch();
    sprintf(statusline, "[key '%i']", c);
    if(c=='h') mv_prevch();
    if(c=='l') mv_nextch();
    if(c=='j') mv_nextln();
    if(c=='k') mv_prevln();
    if(c=='H') cursor.x = 0;
    if(c=='L') cursor.x = strlen(id2str(lines, cursor.y))-1;
    if(c=='d') screendown();
    if(c=='u') screenup();
    if(c=='D') cursor.y = lines.size-1;
    if(c=='U') cursor.y = 0;
    if(c=='g') gotostr();
    if(c=='F') get_search_template();
    if(c=='f') findnext();
    if(c=='w') writefile(filename);
    if(c=='W') writeas();
    if(c=='m') setmark();
    if(c=='c') {
      clean_clipboard();
      clipboard = copy(lines, marker.y, cursor.y);
    }
    if(c=='o') { add_undo_copy(); newstr("\n"); }
    if(c=='i') { add_undo_copy(); insert(); }
    if(c=='r') { add_undo_copy(); replace_char(getch()); }
    if(c=='x') { add_undo_copy(); removechar(); }
    if(c=='X') {
      add_undo_copy();
      removelines(&lines, marker.y, 1 + cursor.y - marker.y);
      cursor.y = marker.y;
    }
    if(c=='p') { add_undo_copy(); paste(&lines, clipboard, cursor.y); }
    if(c=='[') undo();
    if(c==']') redo();
    if(c=='q') quit();
    correct_scr();
    correct_x();
    draw();
  }
}

void
init(){
  setlocale(LC_ALL,"");
  initscr();
  noecho();
  getmaxyx(stdscr, screen_size.y, screen_size.x);
  screen_size.y--;
}

int
main(int ac, char **av){
  init();
  if(ac == 2){
    strcpy(filename, av[1]);
    readfile(filename);
  }else{
    newstr("");
  }
  draw();
  mainloop();
  clear();
  endwin();
  return(0);
}

