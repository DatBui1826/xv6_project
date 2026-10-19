#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

// Hàm quét bộ nhớ và lọc các chuỗi rác
void scan_and_print(char *mem, int size) {
    char buffer[128];
    int len = 0;

    for(int i = 0; i < size; i++) {
        char c = mem[i];
        // Chỉ nhận các ký tự chữ cái và chữ số
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            if (len < 127) {
                buffer[len++] = c;
            }
        } else {
            // Chỉ in ra nếu chuỗi 6 ký tự trở lên để loại bỏ mã máy rác
            if (len >= 6) {
                buffer[len] = '\0';
                printf("%s\n", buffer);
            }
            len = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    // Quét vùng stack hiện tại, đề phòng trang nhớ rơi vào stack
    char temp;
    uint64 stack_page_start = ((uint64)&temp) & ~0xFFF;
    scan_and_print((char*)stack_page_start, 4096);

    // Cấp phát 32 trang nhớ = 131072 bytes để quét vùng heap cũ hệ thống
    char *heap = sbrk(131072);
    scan_and_print(heap, 131072);

    exit(0);
}