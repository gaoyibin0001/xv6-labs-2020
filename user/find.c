#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ){
    // printf("filename %s\n", p);
    return p;
  }
    
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), 0, DIRSIZ-strlen(p));
//   printf("filename %s\n", buf);
  return buf;
}

void
find(char *path, char *pattern)
{
  
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if(st.type != T_DIR){
    fprintf(2, "find: must be a dir, not path %s\n");
    close(fd);
    return;
  }

  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
    printf("ls: path too long\n");
    close(fd);
    return;
  }

    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if((de.inum == 0) | (strcmp(de.name, ".")==0) | (strcmp(de.name, "..")==0))
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
    //   printf("pattern %s\n", pattern);
    //   printf("name %s\n", fmtname(buf));
      if(st.type == T_DIR){
        // printf("%s\n", buf);
        find(buf, pattern);
      }
      // else if(st.type == T_FILE && strcmp(pattern, fmtname(buf))==0){
      else if(st.type == T_FILE && strcmp(pattern, de.name)==0){
        printf("%s\n", buf);
      }
    //   filename = fmtname(buf);     
    }
    
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: find path pattern...");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}
