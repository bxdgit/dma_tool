#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// 函数声明
void print_usage(const char *prog_name);
int do_read(unsigned long long addr, unsigned long long size, const char *output_file);
int do_write(unsigned long long addr, unsigned long long size, const char *data_hex);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    unsigned long long addr, size;

    if (strcmp(argv[1], "read") == 0) {
        if (argc != 5) {
            fprintf(stderr, "错误: 'read' 操作需要 3 个参数\\n");
            print_usage(argv[0]);
            return 1;
        }
        addr = strtoull(argv[2], NULL, 16);
        size = strtoull(argv[3], NULL, 10);
        const char *output_file = argv[4];
        return do_read(addr, size, output_file);
    } else if (strcmp(argv[1], "write") == 0) {
        if (argc != 5) {
            fprintf(stderr, "错误: 'write' 操作需要 3 个参数\\n");
            print_usage(argv[0]);
            return 1;
        }
        addr = strtoull(argv[2], NULL, 16);
        size = strtoull(argv[3], NULL, 10);
        const char *data_hex = argv[4];
        if (strlen(data_hex) != size * 2) {
            fprintf(stderr, "错误: 十六进制数据字符串的长度应为指定大小的两倍 (每个字节2个字符)\n");
            return 1;
        }
        return do_write(addr, size, data_hex);
    } else {
        fprintf(stderr, "错误: 未知的操作 '%s'\\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "用法:\n");
    fprintf(stderr, "  %s read <物理地址(hex)> <长度(byte)> <输出文件名>\n", prog_name);
    fprintf(stderr, "  %s write <物理地址(hex)> <长度(byte)> <十六进制数据>\n", prog_name);
}

int do_read(unsigned long long addr, unsigned long long size, const char *output_file) {
    const char *dev_name = "/dev/xdma0_c2h_0"; // C2H for Card-to-Host (read)
    int fd = open(dev_name, O_RDONLY);
    if (fd < 0) {
        perror("打开读取设备(/dev/xdma0_c2h_0)失败");
        return 1;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 0) {
        perror("sysconf(_SC_PAGESIZE) 失败");
        close(fd);
        return 1;
    }

    // 计算页面对齐的地址和大小
    unsigned long long start_offset = addr % page_size;
    unsigned long long aligned_start_addr = addr - start_offset;
    unsigned long long total_bytes_needed = size + start_offset;
    unsigned long long aligned_read_size = ((total_bytes_needed + page_size - 1) / page_size) * page_size;

    char *buffer = NULL;
    if (posix_memalign((void **)&buffer, page_size, aligned_read_size) != 0) {
        fprintf(stderr, "分配页面对齐内存失败\n");
        close(fd);
        return 1;
    }

    // 使用 lseek 设定对齐后的物理内存地址
    if (lseek(fd, aligned_start_addr, SEEK_SET) == (off_t)-1) {
        perror("lseek 失败");
        free(buffer);
        close(fd);
        return 1;
    }
    
    // 从设定的地址读取对齐后的数据块
    ssize_t bytes_read = read(fd, buffer, aligned_read_size);
    if (bytes_read < 0) {
        perror("从设备读取失败");
        free(buffer);
        close(fd);
        return 1;
    }
    
    if ((unsigned long long)bytes_read != aligned_read_size) {
        fprintf(stderr, "警告: 读取的字节数 (%zd) 与请求的对齐大小 (%llu) 不匹配\n", bytes_read, aligned_read_size);
    }

    FILE *out_fp = fopen(output_file, "wb");
    if (!out_fp) {
        perror("打开输出文件失败");
        free(buffer);
        close(fd);
        return 1;
    }

    // 从对齐的缓冲区中写入实际请求的数据
    fwrite(buffer + start_offset, 1, size, out_fp);
    fclose(out_fp);

    printf("成功从地址 0x%llx 读取 %llu 字节到文件 '%s' (对齐读取 %llu 字节)\n", addr, size, output_file, aligned_read_size);

    free(buffer);
    close(fd);
    return 0;
}

// 将十六进制字符转换为整数值
int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int do_write(unsigned long long addr, unsigned long long size, const char *data_hex) {
    const char *read_dev_name = "/dev/xdma0_c2h_0";
    const char *write_dev_name = "/dev/xdma0_h2c_0";
    int read_fd = -1, write_fd = -1;
    char *buffer = NULL;

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 0) {
        perror("sysconf(_SC_PAGESIZE) 失败");
        return 1;
    }

    // 计算页面对齐的地址和大小
    unsigned long long start_offset = addr % page_size;
    unsigned long long aligned_start_addr = addr - start_offset;
    unsigned long long total_bytes_needed = size + start_offset;
    unsigned long long aligned_rw_size = ((total_bytes_needed + page_size - 1) / page_size) * page_size;

    if (posix_memalign((void **)&buffer, page_size, aligned_rw_size) != 0) {
        fprintf(stderr, "分配页面对齐内存失败\n");
        return 1;
    }

    // 读-改-写 (Read-Modify-Write)
    // 1. 读取整个对齐的块
    read_fd = open(read_dev_name, O_RDONLY);
    if (read_fd < 0) {
        perror("打开读取设备(/dev/xdma0_c2h_0)进行 RMW 操作失败");
        free(buffer);
        return 1;
    }
    if (lseek(read_fd, aligned_start_addr, SEEK_SET) == (off_t)-1) {
        perror("lseek 读取设备失败");
        free(buffer);
        close(read_fd);
        return 1;
    }
    ssize_t bytes_read = read(read_fd, buffer, aligned_rw_size);
    if (bytes_read < 0 || (unsigned long long)bytes_read != aligned_rw_size) {
        fprintf(stderr, "RMW 期间读取原始数据失败 (读取 %zd / %llu 字节)\n", bytes_read, aligned_rw_size);
        perror("读取错误");
        free(buffer);
        close(read_fd);
        return 1;
    }
    close(read_fd);

    // 2. 在内存中修改需要写入的部分
    for (unsigned long long i = 0; i < size; ++i) {
        int high = hex_char_to_int(data_hex[i * 2]);
        int low = hex_char_to_int(data_hex[i * 2 + 1]);
        if (high == -1 || low == -1) {
            fprintf(stderr, "错误: 无效的十六进制字符\n");
            free(buffer);
            return 1;
        }
        buffer[start_offset + i] = (high << 4) | low;
    }
    
    // 3. 将修改后的整个块写回
    write_fd = open(write_dev_name, O_WRONLY);
    if (write_fd < 0) {
        perror("打开写入设备(/dev/xdma0_h2c_0)失败");
        free(buffer);
        return 1;
    }
    if (lseek(write_fd, aligned_start_addr, SEEK_SET) == (off_t)-1) {
        perror("lseek 写入设备失败");
        free(buffer);
        close(write_fd);
        return 1;
    }
    ssize_t bytes_written = write(write_fd, buffer, aligned_rw_size);
    if (bytes_written < 0) {
        perror("写入设备失败");
        free(buffer);
        close(write_fd);
        return 1;
    }
    if ((unsigned long long)bytes_written != aligned_rw_size) {
        fprintf(stderr, "警告: 写入的字节数 (%zd) 与对齐后的大小 (%llu) 不匹配\n", bytes_written, aligned_rw_size);
    }

    printf("成功向地址 0x%llx 写入 %llu 字节 (页面对齐写入 %llu 字节)\n", addr, size, aligned_rw_size);

    free(buffer);
    close(write_fd);
    return 0;
} 