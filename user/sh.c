// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

#define MAX_HISTORY 10
char history[MAX_HISTORY][100]; // Mảng 2 chiều lưu 10 lệnh, mỗi lệnh tối đa 100 ký tự
int history_count = 0; // Số lệnh đã lưu
int current_history = 0; // Vị trí đang duyệt tới, dành cho phím mũi tên

void save_history(char *buf) {
  // Không lưu các dòng trống hoặc lệnh gọi lại bằng dấu !
  if(buf[0] == '\n' || buf[0] == '\0' || buf[0] == '!') return;

  char temp[100];
  strcpy(temp, buf);

  // Xóa dấu xuống dòng '\n' ở cuối câu để in ra cho đẹp
  int len = strlen(temp);
  if(len > 0 && temp[len-1] == '\n') {
    temp[len-1] = '\0';
  }

  // Dịch mảng lên nếu đã chứa đủ 10 lệnh
  if(history_count == MAX_HISTORY) {
    for(int i = 0; i < MAX_HISTORY - 1; i++) {
      strcpy(history[i], history[i+1]);
    }
    strcpy(history[MAX_HISTORY - 1], temp);
  } else {
    strcpy(history[history_count], temp);
    history_count++;
  }

  current_history = history_count; // Dùng cho phím mũi tên sau này
}

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
void runcmd(struct cmd *) __attribute__((noreturn));

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0) {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

// Hàm hỗ trợ tự động điền tên file khi nhấn Tab
void autocomplete(char *buf, int *len) {
  // Tìm từ cuối mà người dùng đang gõ dở
  int start = *len;
  while (start > 0 && buf[start - 1] != ' ') {
    start--;
  }
  int prefix_len = *len - start;
  if (prefix_len == 0) return; // Chưa gõ chữ nào thì không điền

  // Mở thư mục hiện tại (".")
  int fd = open(".", 0); // 0 là cờ O_RDONLY (chỉ đọc)
  if (fd < 0) return;

  struct dirent de;
  char match[16]; // Tên file trong xv6 dài tối đa 14 ký tự (DIRSIZ)
  int match_count = 0;

  // Đọc từng file trong thư mục
  while (read(fd, &de, sizeof(de)) == sizeof(de)) {
    if (de.inum == 0) continue; // Bỏ qua các file đã bị xóa

    // Kiểm tra xem tên file có bắt đầu bằng cụm từ đang gõ không
    int is_match = 1;
    for (int j = 0; j < prefix_len; j++) {
      if (de.name[j] != buf[start + j]) {
        is_match = 0;
        break;
      }
    }

    // Khớp thì lưu lại tên file đó
    if (is_match) {
      memmove(match, de.name, 14);
      match[14] = '\0'; // Đảm bảo chuỗi kết thúc đúng cách
      match_count++;
    }
  }
  close(fd);

  // Nếu chỉ đúng 1 file khớp, tự động điền nốt phần còn thiếu
  if (match_count == 1) {
    int rest_len = strlen(match) - prefix_len;
    for (int j = 0; j < rest_len; j++) {
      buf[*len] = match[prefix_len + j];
      fprintf(2, "%c", buf[*len]); // In chữ đó ra màn hình
      (*len)++;
    }
  }
}

int
getcmd(char *buf, int nbuf)
{
  struct stat st;

  // Dùng fstat để kiểm tra file descriptor 0 (Standard Input)
  // Nếu loại file st.type là T_DEVICE, tức là đang nhập trực tiếp từ console
  // thì mới in dấu "$ "
  if(fstat(0, &st) == 0 && st.type == T_DEVICE) {
    fprintf(2, "$ ");
  }

  memset(buf, 0, nbuf);
  int i = 0;

  // Vòng lặp đọc từng ký tự
  while(i < nbuf - 1) {
    char c;
    // Đọc 1 ký tự từ bàn phím
    if(read(0, &c, 1) < 1) break;

    if(c == '\n' || c == '\r') {
      buf[i++] = '\n';
      break;
    }
    // Phím Esc = ASCII 27 - bắt đầu tổ hợp phím mũi tên
    else if (c == 27) {
      char seq[2];
      read(0, &seq[0], 1);
      read(0, &seq[1], 1);

      if(seq[0] == '[') {
        if(seq[1] == 'A') { // phím UP
          if(current_history > 0) {
            current_history--;
            // Xóa dòng lệnh hiện tại trên console
            while(i > 0) { fprintf(2, "\b \b"); i--; }
            // Nạp và in lệnh cũ ra
            strcpy(buf, history[current_history]);
            i = strlen(buf);
            fprintf(2, "%s", buf);
          }
        }
        else if(seq[1] == 'B') { // phím DOWN
          if(current_history < history_count - 1) {
            current_history++;
            while(i > 0) { fprintf(2, "\b \b"); i--; }
            strcpy(buf, history[current_history]);
            i = strlen(buf);
            fprintf(2, "%s", buf);
          }
          else if(current_history == history_count - 1) {
            // Khi duyệt xuống đáy thì xóa trắng dòng
            current_history++;
            while(i > 0) { fprintf(2, "\b \b"); i--; }
            buf[0] = '\0';
            i = 0;
          }
        }
      }
    }
    // phím Backspace (8 hoặc 127)
    else if (c == '\b' || c == 127) {
      if(i > 0) {
        i--;
        // Kernel của xv6 đã tự xử lý xóa hiển thị, ta chỉ lùi biến i
      }
    }
    // phím Tab (9)
    else if (c == '\t') {
      autocomplete(buf, &i);
    }
    // Các ký tự bình thường
    else {
      buf[i++] = c;
    }
  }

  if(buf[0] == 0) return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while ((fd = open("console", O_RDWR)) >= 0) {
    if (fd >= 3) {
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0) {
    char *cmd = buf;
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == '\n') // is a blank command
      continue;

    // Bắt lệnh in danh sách lịch sử
    if (strcmp(cmd, "history\n") == 0) {
      save_history(cmd); // Vẫn lưu chuỗi "history" vào bộ nhớ
      for (int i = 0; i < history_count; i++) {
        fprintf(2, "%d %s\n", i + 1, history[i]);
      }
      continue;
    }

    // 2. Xử lý gọi lại lệnh cũ !! hoặc !n
    if (cmd[0] == '!') {
      int idx = -1;
      if (cmd[1] == '!' && cmd[2] == '\n') {
        idx = history_count - 1; // !!: gọi lại lệnh cuối cùng
      } else {
        int n = atoi(cmd + 1);   // !n: lấy số n do người dùng nhập
        idx = n - 1;             // Chỉ số mảng bắt đầu từ 0
      }

      // Nếu chỉ số hợp lệ, nạp lại lệnh từ bộ nhớ
      if (idx >= 0 && idx < history_count) {
        fprintf(2, "%s\n", history[idx]); // In lệnh ra trước khi chạy
        strcpy(cmd, history[idx]);        // Copy ngược lệnh cũ đè vào cmd

        // Thêm \n để shell phân tích đúng ngữ pháp
        int len = strlen(cmd);
        cmd[len] = '\n';
        cmd[len + 1] = '\0';
      } else {
        fprintf(2, "cannot find command in history\n");
        continue;
      }
    }

    // Lưu lệnh đang chạy vào lịch sử
    save_history(cmd);

    if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' ') {
      // Chdir must be called by the parent, not the child.
      cmd[strlen(cmd) - 1] = 0; // chop \n
      if (chdir(cmd + 3) < 0)
        fprintf(2, "cannot cd %s\n", cmd + 3);
    } else {
      if (fork1() == 0)
        runcmd(parsecmd(cmd));
      wait(0);
    }
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
