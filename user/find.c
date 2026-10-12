// user/find.c
// Cách dùng: find <dir> <name> [-exec cmd [args...]]
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "user/user.h"

static char *cmd_argv[MAXARG];  // cmd + args của -exec
static int cmd_argc = 0;        // 0 nghĩa là không dùng -exec

// Trả về phần tên cuối cùng của path
static char *
basename(char *path)
{
  char *p;
  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  return p + 1;
}

// Chạy "cmd args... path"
static void
run_exec(char *path)
{
  char *argv[MAXARG];
  int i;

  for(i = 0; i < cmd_argc; i++)
    argv[i] = cmd_argv[i];
  argv[cmd_argc] = path;
  argv[cmd_argc + 1] = 0;

  int pid = fork();
  if(pid < 0){
    fprintf(2, "find: fork failed\n");
    return;
  }
  if(pid == 0){
    exec(argv[0], argv);
    fprintf(2, "find: exec %s failed\n", argv[0]);
    exit(1);
  }
  wait(0);
}

static void
find(char *path, char *name)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if(strcmp(basename(path), name) == 0){
    if(cmd_argc > 0)
      run_exec(path);
    else
      printf("%s\n", path);
  }

  if(st.type == T_DIR){
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
      fprintf(2, "find: path too long\n");
      close(fd);
      return;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      find(buf, name);
    }
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 3){
    fprintf(2, "usage: find <dir> <name> [-exec cmd [args...]]\n");
    exit(1);
  }

  for(i = 3; i < argc; i++){
    if(strcmp(argv[i], "-exec") == 0){
      if(i + 1 >= argc){
        fprintf(2, "find: -exec needs a command\n");
        exit(1);
      }
      for(i++; i < argc; i++){
        if(cmd_argc >= MAXARG - 2){
          fprintf(2, "find: too many arguments\n");
          exit(1);
        }
        cmd_argv[cmd_argc++] = argv[i];
      }
      break;
    }
  }

  find(argv[1], argv[2]);
  exit(0);
}