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

List lines = {0, 0, 0};
List clipboard = {0, 0, 0};
Pos cursor = {0, 0};   /* cursor position */
Pos mark   = {0, 0};
Pos scrpos = {0, 0};   /* current screen position */
Pos scr;               /* screen size */
char search_template[100] = "...";
char statusline[200] = "[ozkriff's ed]";
char fname[100];         /* file name */

void
readfile(char *fname){
  FILE *f = fopen(fname, "r");
  char buffer[300];
  while(fgets(buffer, 299, f)){
    int len = strlen(buffer);
    char *s = malloc(len * sizeof(char) + 1);
    strcpy(s, buffer);
    add_node_to_tail(&lines, s);
  }
  fclose(f);
  sprintf(statusline, "[opened '%s']", fname);
}

void
writefile(char *fname){
  Node *nd;
  FILE *f = fopen(fname, "w");
  if(!f){
    puts("NO FILE!");
    exit(1);
  }
  FOR_EACH_NODE(lines, nd){
    char *s = nd->data;
    fputs(s, f);
  }
  fclose(f);
  sprintf(statusline, "[written %s]", fname);
}

void
writeline(char *s){
  int len = strlen(s);
  addnstr(s, scr.x-1);
  if(len > scr.x-1)
    addch('\n');
}

void
writelines(int from, int n){
  char *s;
  Node *nd = lines.head;
  int i = 0;
  FOR_EACH_NODE(lines, nd){
    if(i >= from){
      if(i == n+from)
        return;
      s = nd->data;
      writeline(s);
    }
    i++;
  }
}

void
draw_statusline(){
  char s[120];
  sprintf(s, "%i:%i: %s",
      cursor.y, cursor.x, statusline);
  mvprintw(scr.y, 0, s);
}

void
draw(){
  clear();
  move(0, 0);
  writelines(scrpos.y, scr.y);
  draw_statusline();
  move(cursor.y-scrpos.y, cursor.x);
  refresh();
}

Node *
id2node(int line){
  Node *nd = lines.head;
  int i = 0;
  FOR_EACH_NODE(lines, nd){
    if(i == line)
      return(nd);
    i++;
  }
  return(NULL);
}

char *
id2str(int line){
  char *s = id2node(line)->data;
  return(s);
}

void
mv_nextln(){
  char *s;
  int n;
  if(cursor.y == (lines.size-1))
    return;
  cursor.y++;
  s = id2str(cursor.y);
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
  s = id2str(cursor.y);
  n = strlen(s)-1;
  if(cursor.x > n)
    cursor.x = n;
}

void
mv_nextch(){
  char *s = id2str(cursor.y);
  cursor.x++;
  if(s[cursor.x] == '\0'){
    mv_nextln();
    cursor.x = 0;
  }
}

void
mv_prevch(){
  char *s;
  if(cursor.x == 0){
    mv_prevln();
    s = id2str(cursor.y);
    cursor.x = strlen(s)-1;
  }else{
    cursor.x--;
  }
}

void
newstr(char *data){
  char *s = malloc(strlen(data) * sizeof(char) + 1);
  strcpy(s, data);
  insert_node(&lines, s, id2node(cursor.y));
  mv_nextln();
  cursor.x = 0;
}

void
replace_char(char c){
  char *s = id2str(cursor.y);
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
  sprintf(statusline, "[insert mode]");
  draw();
  while( (c=getch()) != 27){
    if(c!='\n'){
      str = id2str(cursor.y);
      nstr = calloc(strlen(str)+1+1, sizeof(char));
      strncpy(nstr, str, cursor.x);
      strcpy(nstr + cursor.x + 1, str + cursor.x);
      free(str);
      id2node(cursor.y)->data = nstr;
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
  for(i=0; i<scr.y/2; i++)
    mv_prevln();
}

void
screendown(){
  int i;
  for(i=0; i<scr.y/2; i++)
    mv_nextln();
}

void
removechar(){
  char *s = id2str(cursor.y);
  if(s[cursor.x] == '\n'){
    int len1 = strlen(s) + 1;
    /* next string */
    char *s2 = id2str(cursor.y+1);
    int len2 = strlen(s2) + 1;
    /* new string */
    char *ns = malloc(len1 + len2);
    strcpy(ns, s);
    strcpy(ns + len1 - 2, s2);
    delete_node(&lines, id2node(cursor.y+1));
    id2node(cursor.y)->data = ns;
    free(s);
  }
  strcpy(s+cursor.x, s+cursor.x+1);
}

void
gotostr(){
  int n;
  scanw("%i", &n);
  cursor.y = n;
}

void
correct_scr(){
  while(cursor.y < scrpos.y)
    scrpos.y--;
  while(cursor.y >= scrpos.y+scr.y)
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
  nd = id2node(y);
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
get_findme(){
  scanw("%s", search_template);
  findnext();
}

void
removeln(){
  delete_node(&lines, id2node(cursor.y));
}

void
writeas(){
  char newfname[100]; /* new file name */
  scanw("%s", newfname);
  writefile(newfname);
}

void
setmark(){
  mark = cursor;
}

void
clean_clipboard(){
  while(clipboard.size != 0)
    delete_node(&clipboard, clipboard.head);
}

/* copy one line to clipboard */
void
copy_line(int line){
  char *s;
  int len;
  char *s2;
  s = id2str(line);
  len = strlen(s);
  s2 = malloc(len+1);
  strcpy(s2, s);
  add_node_to_head(&clipboard, s2);
}

/* paste one line from clipboard */
void
paste_line(){
  char *s = extruct_data(&clipboard, clipboard.head);
  if(!s)
    exit(1);
  insert_node(&lines, s, id2node(cursor.y));
}

void
copy(){
  int i;
  clean_clipboard();
  if(mark.y > cursor.y)
    return;
  for(i = mark.y; cursor.y >= i; i++)
    copy_line(i);
}

void
paste(){
  Node *n;
  FOR_EACH_NODE(clipboard, n){
    char *original = n->data;
    int length = strlen(original);
    char *new = calloc(length +1 +1, sizeof(char));
    strcpy(new, original);
    insert_node(&lines, new, id2node(cursor.y));
  }
}

void
correct_x(){
  int len = strlen(id2str(cursor.y));
  if(cursor.x >= len)
    cursor.x = len;
  if(cursor.x < 0)
    cursor.x = 0;
}

void
mainloop(){
  int c = ' ';
  while(c != 27 && c != 'q'){
    c = getch();
    /*printw("[%i]", c);*/
    sprintf(statusline, "[key '%i']", c);
    if(c=='h') mv_prevch();
    if(c=='l') mv_nextch();
    if(c=='j') mv_nextln();
    if(c=='k') mv_prevln();
    if(c=='H') cursor.x = 0;
    if(c=='L') cursor.x = strlen(id2str(cursor.y))-1;
    if(c=='d') screendown();
    if(c=='u') screenup();
    if(c=='D') cursor.y = lines.size-1;
    if(c=='U') cursor.y = 0;
    if(c=='r') replace_char(getch());
    if(c=='i') insert();
    if(c=='x') removechar();
    if(c=='g') gotostr();
    if(c=='o') newstr("\n");
    if(c=='F') get_findme();
    if(c=='f') findnext();
    if(c=='w') writefile(fname);
    if(c=='W') writeas();
    if(c=='X') removeln();
    if(c=='m') setmark();
    if(c=='c') copy();
    if(c=='p') paste();
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
  getmaxyx(stdscr, scr.y, scr.x);
  scr.y--;
}

int
main(int ac, char **av){
  init();
  if(ac == 2){
    strcpy(fname, av[1]);
    readfile(fname);
  }else{
    newstr("");
  }
  draw();
  mainloop();
  clear();
  endwin();
  return(0);
}

