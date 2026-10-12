#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data, int len);

int
main(int argc, char *argv[])
{
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2026};
    memdump("ii", (char *)a, sizeof(a));

    printf("Example 2:\n");
    memdump("S", "a string", sizeof("a string"));

    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *)&s, sizeof(s));

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char *)&example, sizeof(example));

    printf("Example 5:\n");
    memdump("sccccc", (char *)&example, sizeof(example));
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data, n);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data, int len)
{
  int pos = 0;   // số byte của data đã dùng

  for(; *fmt; fmt++){
    char f = *fmt;
    int need;    // số byte tối thiểu mà ký tự format này cần

    switch(f){
    case 'i': need = 4; break;
    case 'p': need = 8; break;
    case 'h': need = 2; break;
    case 'c': need = 1; break;
    case 's': need = 8; break;
    case 'S': need = 0; break;   // dùng phần còn lại, có thể rỗng
    default:  continue;          // bỏ qua ký tự format lạ
    }

    if(len - pos < need){
      printf("memdump: not enough data for '%c'\n", f);
      return;
    }

    // Dùng memmove vào biến cục bộ để tránh đọc lệch alignment
    // (data + pos có thể không chia hết cho 2/4/8).
    if(f == 'i'){
      int v;
      memmove(&v, data + pos, sizeof(v));
      printf("%d\n", v);
      pos += need;
    } else if(f == 'p'){
      uint64 v;
      memmove(&v, data + pos, sizeof(v));
      printf("%lx\n", v);
      pos += need;
    } else if(f == 'h'){
      short v;
      memmove(&v, data + pos, sizeof(v));
      printf("%d\n", (int)v);
      pos += need;
    } else if(f == 'c'){
      printf("%c\n", data[pos]);
      pos += need;
    } else if(f == 's'){
      char *ptr;
      memmove(&ptr, data + pos, sizeof(ptr));
      printf("%s\n", ptr);
      pos += need;
    } else {  // 'S': in đến '\0' hoặc hết len, tùy cái nào đến trước
      while(pos < len && data[pos] != '\0'){
        printf("%c", data[pos]);
        pos++;
      }
      printf("\n");
    }
  }
}