
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <locale.h>
#include <ncurses.h>


typedef struct Node Node;
struct Node {
  Node * n; /* pointer to [n]ext node or NULL */
  Node * p; /* pointer to [p]revious node or NULL */
  void * d; /* pointer to [d]ata */
};

typedef struct List List;
struct List {
  Node * h; /* pointer to first ([h]ead) node */
  Node * t; /* pointer to last ([t]ail) node */
  int count; /* number of nodes in list */
};


#define l_addhead(list_p, node_p)          l_insert_node(list_p, node_p, NULL)
#define l_addnext(list_p, node_p, after_p) l_insert_node(list_p, node_p, after_p)
#define l_addtail(list_p, node_p)          l_insert_node(list_p, node_p, (list_p)->t)

#define FOR_EACH_NODE(list, node_p) \
  for(node_p=(list).h; node_p; node_p=node_p->n)


/* Create node <new> that points to <data> and insert 
  this node into list.
  If <after>==NULL, then <new> will be added at the head
  of the list, else it will be added following <after>.
  Only pointer to data is stored, no copying! */
void
l_insert_node (List * list, void * data, Node * after){
  Node * new = malloc(sizeof(Node));
  new->d = data;
  if(after){
    new->n = after->n;
    new->p = after;
    after->n = new;
  }else{
    new->n = list->h;
    new->p = after;
    list->h = new;
  }
  if(new->n)  new->n->p=new;  else  list->t=new;
  list->count++;
}


/* Extructs node from list, returns pointer to this node.
  No memory is freed */
Node *
l_extruct_node (List * list, Node * nd){
  if(nd){
    if(nd->n)  nd->n->p=nd->p;  else  list->t=nd->p;
    if(nd->p)  nd->p->n=nd->n;  else  list->h=nd->n;
    list->count--;
  }
  return(nd);
}


/* Delete data and node. */
void
l_delete_node (List * list, Node * nd){
  Node * tmp = l_extruct_node(list, nd);
  free(tmp->d);
  free(tmp);
}


/* Extruct node from list, delete node,
  return pointer to data. */
void *
l_extruct_data (List * list, Node * old){
  Node * node = l_extruct_node(list, old);
  void * data = node->d;
  free(node);
  return(data);
}


/* ------------------ main code ------------------- */


typedef struct Vec2i { int y; int x; } Vec2i;


List lines = {0, 0, 0};
List clipboard = {0, 0, 0};
Vec2i cursor = {0, 0};   /* cursor position */
Vec2i mark   = {0, 0};
Vec2i scrpos = {0, 0};   /* current screen position */
Vec2i scr;               /* screen size */
char findme[100] = "search template";
char statusline[200] = "[ozkriff's ed]";
char fname[100];         /* file name */


void
readfile(char * fname){
  FILE * f = fopen(fname, "r");
  char buffer[300];
  while(fgets(buffer, 299, f)){
    int len = strlen(buffer);
    char * s = malloc(len * sizeof(char) + 1);
    strcpy(s, buffer);
    l_addtail(&lines, s);
  }
  fclose(f);
  sprintf(statusline, "[opened '%s']", fname);
}


void
writefile(char * fname){
  Node * nd;
  FILE * f = fopen(fname, "w");
  if(!f){
    puts("NO FILE!");
    exit(1);
  }
  FOR_EACH_NODE(lines, nd){
    char * s = nd->d;
    fputs(s, f);
  }
  fclose(f);
  sprintf(statusline, "[written %s]", fname);
}


void
writeline(char * s){
  int len = strlen(s);
  addnstr(s, scr.x-1);
  if(len > scr.x-1)
    addch('\n');
}


void
writeline_color(char *s){
  int len = strlen(s);
/*
  int nocollen = 20; // no color len
  addnstr(s, nocollen);
  attron(COLOR_PAIR(1));
  addnstr(s+nocollen, scr.x-nocollen-1);
  addnstr(s+nocollen, scr.x-nocollen-1);
  attroff(COLOR_PAIR(1));
*/
  int i;
  for(i=0; i<len; i++){
    if(i==20)
      attron(COLOR_PAIR(1));
    addch(s[i]);
  }
  attroff(COLOR_PAIR(1));
  if(len > scr.x-1)
    addch('\n');
}


void
writelines(int from, int n){
  char * s;
  Node * nd = lines.h;
  int i = 0;
  FOR_EACH_NODE(lines, nd){
    if(i >= from){
      if(i == n+from)
        return;
      s = nd->d;
      writeline(s);
      /* writeline_color(s); */
    }
    i++;
  }
}


void
draw_statusline(){
  mvprintw(scr.y, 0, statusline);
}


void
draw(){
  move(0, 0);
  writelines(scrpos.y, scr.y);
  draw_statusline();
  move(cursor.y-scrpos.y, cursor.x);
  refresh();
}


Node *
id2node(int line){
  Node * nd = lines.h;
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
  char * s = id2node(line)->d;
  return(s);
}


void
mv_nextln(){
  char * s;
  int n;
  if(cursor.y == (lines.count-1))
    return;
  cursor.y++;
  s = id2str(cursor.y);
  n = strlen(s)-1;
  if(cursor.x > n)
    cursor.x = n;
}


void
mv_prevln(){
  char * s;
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
  char * s = id2str(cursor.y);
  cursor.x++;
  if(s[cursor.x] == '\0'){
    mv_nextln();
    cursor.x = 0;
  }
}


void
mv_prevch(){
  char * s;
  if(cursor.x == 0){
    mv_prevln();
    s = id2str(cursor.y);
    cursor.x = strlen(s)-1;
  }else{
    cursor.x--;
  }
}


void
newstr(char * data){
  char * s = malloc(strlen(data) * sizeof(char) + 1);
  strcpy(s, data);
  l_insert_node(&lines, s, id2node(cursor.y));
  mv_nextln();
  cursor.x = 0;
}


void
replace_char(char c){
  char * s = id2str(cursor.y);
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
  char * str;
  char * nstr;
  sprintf(statusline, "[INSERT MODE]");
  draw();
  while( (c=getch()) != 27){
    if(c!='\n'){
      str = id2str(cursor.y);
      nstr = calloc(strlen(str)+1+1, sizeof(char));
      strncpy(nstr, str, cursor.x);
      strcpy(nstr + cursor.x + 1, str + cursor.x);
      free(str);
      id2node(cursor.y)->d = nstr;
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
  char * s = id2str(cursor.y);
  if(s[cursor.x] == '\n'){
    int len1 = strlen(s) + 1;

    /* next string */
    char * s2 = id2str(cursor.y+1);
    int len2 = strlen(s2) + 1;

    /* new string */
    char * ns = malloc(len1 + len2);
    strcpy(ns, s);
    strcpy(ns + len1 - 2, s2);

    l_delete_node(&lines, id2node(cursor.y+1));
    id2node(cursor.y)->d = ns;
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
get_offset(char * s, char * findme){
  int o; /* offset */
  char *p, *p2;
  for(o=0; s[o]; o++) {
    p = &s[o];
    p2 = findme;
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
  Node * nd;
  int y = cursor.y + 1;
  if(y >= lines.count)
    y = 0;
  nd = id2node(y);
  while(nd && y < lines.count){
    char * s = nd->d;
    if(strstr(s, findme)){
      cursor.y = y;
      cursor.x = get_offset(s, findme);
      return;
    }
    nd = nd->n;
    y++;
  }
}


void
get_findme(){
  scanw("%s", findme);
  findnext();
}


void
removeln(){
  l_delete_node(&lines, id2node(cursor.y));
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
  l_addhead(&clipboard, s2);
}


/* paste one line from clipboard */
void
paste_line(){
  char *s = l_extruct_data(&clipboard, clipboard.h);
  if(!s)
    exit(1);
  l_insert_node(&lines, s, id2node(cursor.y));
}


void
copy(){
  int i = mark.y;
  while(cursor.y != i){
    copy_line(i);
    i++;
  }
}


void
paste(){
  while(clipboard.count > 0)
    paste_line();
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
    if(c=='D') cursor.y = lines.count-1;
    if(c=='U') cursor.y = 0;
    if(c=='r') replace_char(getch());
    if(c=='i') insert();
    if(c=='x') removechar();
    if(c=='g') gotostr();
    if(c=='o') newstr("\n\0");
    if(c=='F') get_findme();
    if(c=='f') findnext();
    if(c=='w') writefile(fname);
    if(c=='W') writeas();
    if(c=='X') removeln();
    if(c=='m') setmark();
    if(c=='c') copy();
    if(c=='p') paste();
    correct_scr();
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
/*
  start_color();
  init_pair(1, COLOR_RED, COLOR_BLACK);
  attroff(COLOR_PAIR(1));
*/
}


int
main(int ac, char **av){
  if(ac!=2){
    puts("./ed <filename>");
    exit(1);
  }
  strcpy(fname, av[1]);
  init();
  readfile(fname);
  draw();
  mainloop();
  endwin();
  return(0);
}

