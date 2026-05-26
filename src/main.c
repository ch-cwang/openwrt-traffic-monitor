/**
 * @file main.c
 * @brief OpenWrt 实时网络流量监控守护进程
 * 
 * 本程序通过 libpcap 捕获网络流量，按 IP 地址汇总 RX/TX 统计数据，
 * 并将实时指标输出到 JSON 文件供前端大盘可视化展示。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <pcap.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#define MAX_IP_RECORDS       50
#define DEFAULT_JSON_PATH    "/www/traffic.json"
#define STALE_IP_TIMEOUT_SEC 300  // 如果 5 分钟无流量则释放该 IP 记录
#define HISTORY_WINDOW_SIZE  40
#define DEFAULT_INTERFACE    "br-lan"

/**
 * @struct ip_record_t
 * @brief 存储单个 IP 地址的实时和历史流量统计数据。
 */
typedef struct {
    char ip[INET_ADDRSTRLEN];
    
    uint64_t total_rx_bytes;
    uint64_t total_tx_bytes;
    
    uint64_t rx_peak_rate;
    uint64_t tx_peak_rate;
    
    uint64_t rx_current_rate;
    uint64_t tx_current_rate;
    
    uint64_t rx_history[HISTORY_WINDOW_SIZE];
    uint64_t tx_history[HISTORY_WINDOW_SIZE];
    int history_idx;
    
    uint64_t rx_avg_2s;
    uint64_t rx_avg_10s;
    uint64_t rx_avg_40s;
    
    uint64_t tx_avg_2s;
    uint64_t tx_avg_10s;
    uint64_t tx_avg_40s;
    
    time_t last_active;
} ip_record_t;

ip_record_t g_ip_records[MAX_IP_RECORDS];
int g_num_records = 0;
pthread_mutex_t g_lock;
volatile sig_atomic_t g_running = 1;

/**
 * @brief 捕获信号以实现优雅停机。
 */
void handle_sigint(int sig) {
    g_running = 0;
}

/**
 * @brief 计算过去 'num' 秒的滑动平均值。
 */
uint64_t calc_moving_average(const uint64_t* history, int idx, int num) {
    if (num <= 0) return 0;
    uint64_t sum = 0;
    for (int i = 0; i < num; i++) {
        sum += history[idx];
        idx = (idx - 1 + HISTORY_WINDOW_SIZE) % HISTORY_WINDOW_SIZE;
    }
    return sum / num;
}

// 前向声明 pcap 数据包处理函数
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet);

/**
 * @brief 持续捕获网络数据包的线程函数。
 */
void* capture_thread(void* arg) {
    pcap_t *handle = (pcap_t *)arg;
    pcap_loop(handle, -1, packet_handler, NULL);
    return NULL;
}

/**
 * @brief 查找或分配一个 IP 记录。
 * @param ip_str 字符串形式的 IP 地址。
 * @return 记录的索引，如果表已满则返回 -1。
 */
int get_or_create_ip_idx(const char* ip_str) {
    for (int i = 0; i < g_num_records; i++) {
        if (strcmp(g_ip_records[i].ip, ip_str) == 0) {
            g_ip_records[i].last_active = time(NULL);
            return i;
        }
    }
    
    if (g_num_records < MAX_IP_RECORDS) {
        int idx = g_num_records++;
        memset(&g_ip_records[idx], 0, sizeof(ip_record_t));
        strncpy(g_ip_records[idx].ip, ip_str, INET_ADDRSTRLEN - 1);
        g_ip_records[idx].last_active = time(NULL);
        return idx;
    }
    
    return -1;
}

/**
 * @brief 每次捕获到数据包时触发的回调函数。
 */
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet) {
    if (pkthdr->caplen < sizeof(struct ether_header)) return;
    
    struct ether_header *eth = (struct ether_header *)packet;
    if (ntohs(eth->ether_type) != ETHERTYPE_IP) return;

    struct ip *iph = (struct ip *)(packet + sizeof(struct ether_header));
    char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
    
    inet_ntop(AF_INET, &(iph->ip_src), src, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(iph->ip_dst), dst, INET_ADDRSTRLEN);
    
    unsigned int len = pkthdr->len;

    pthread_mutex_lock(&g_lock);
    
    // 发送方统计：IP 向外发送视为 TX
    int src_idx = get_or_create_ip_idx(src);
    if (src_idx != -1) {
        g_ip_records[src_idx].total_tx_bytes += len;
        g_ip_records[src_idx].tx_current_rate += len;
    }

    // 接收方统计：IP 接收进入视为 RX
    int dst_idx = get_or_create_ip_idx(dst);
    if (dst_idx != -1) {
        g_ip_records[dst_idx].total_rx_bytes += len;
        g_ip_records[dst_idx].rx_current_rate += len;
    }

    pthread_mutex_unlock(&g_lock);
}

int main() {
    char err_buf[PCAP_ERRBUF_SIZE];
    pcap_t* handle;
    const char* device = DEFAULT_INTERFACE;

    // 打开网络设备进行数据包捕获
    handle = pcap_open_live(device, 65535, 1, 1000, err_buf);
    if (handle == NULL) {
        fprintf(stderr, "打开网络设备失败 %s: %s\n", device, err_buf);
        return 1;
    }

    struct bpf_program fp;
    bpf_u_int32 netmask = 0;

    // 编译并应用仅针对 IP 流量的 BPF 过滤器
    if (pcap_compile(handle, &fp, "ip", 0, netmask) == -1) {
        fprintf(stderr, "编译 pcap 过滤器失败: %s\n", pcap_geterr(handle));
        pcap_close(handle);
        return 1;
    }
    
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "设置 pcap 过滤器失败: %s\n", pcap_geterr(handle));
        pcap_freecode(&fp);
        pcap_close(handle);
        return 1;
    }

    pthread_mutex_init(&g_lock, NULL);
    memset(g_ip_records, 0, sizeof(g_ip_records));
    signal(SIGINT, handle_sigint);

    pthread_t tid;
    if (pthread_create(&tid, NULL, capture_thread, (void*)handle) != 0) {
        fprintf(stderr, "创建捕获线程失败\n");
        pcap_freecode(&fp);
        pcap_close(handle);
        pthread_mutex_destroy(&g_lock);
        return 1;
    }

    printf("NetTrail 守护进程已在 %s 接口启动。统计数据正写入 %s\n", device, DEFAULT_JSON_PATH);

    while (g_running) {
        sleep(1);
        
        ip_record_t local_records[MAX_IP_RECORDS];
        int local_record_idx = 0;

        pthread_mutex_lock(&g_lock);
        time_t now = time(NULL);
        
        // 1. 剔除过期的 IP，并更新活跃 IP 的历史记录
        for (int i = 0; i < g_num_records; ) {
            if (now - g_ip_records[i].last_active > STALE_IP_TIMEOUT_SEC) {
                // 通过与最后一个元素交换来剔除
                g_ip_records[i] = g_ip_records[g_num_records - 1];
                g_num_records--;
            } else {
                ip_record_t *ip = &g_ip_records[i];
                int idx = ip->history_idx;
                
                // 将当前 1 秒的流量写入历史记录
                ip->rx_history[idx] = ip->rx_current_rate;
                ip->tx_history[idx] = ip->tx_current_rate;
                
                // 更新峰值速率
                if (ip->rx_peak_rate < ip->rx_current_rate) {
                    ip->rx_peak_rate = ip->rx_current_rate;
                }
                if (ip->tx_peak_rate < ip->tx_current_rate) {
                    ip->tx_peak_rate = ip->tx_current_rate;
                }
                
                // 计算滑动平均值
                ip->rx_avg_2s  = calc_moving_average(ip->rx_history, idx, 2);
                ip->rx_avg_10s = calc_moving_average(ip->rx_history, idx, 10);
                ip->rx_avg_40s = calc_moving_average(ip->rx_history, idx, 40);
                
                ip->tx_avg_2s  = calc_moving_average(ip->tx_history, idx, 2);
                ip->tx_avg_10s = calc_moving_average(ip->tx_history, idx, 10);
                ip->tx_avg_40s = calc_moving_average(ip->tx_history, idx, 40);

                // 推进循环缓冲区的索引
                ip->history_idx = (idx + 1) % HISTORY_WINDOW_SIZE;
                
                // 将一致的状态拷贝到本地缓冲区，以便进行无锁 I/O 操作
                local_records[i] = *ip;
                
                // 清零速率计数器，迎接下一秒
                ip->rx_current_rate = 0;
                ip->tx_current_rate = 0;
                
                i++;
            }
        }
        local_record_idx = g_num_records;
        pthread_mutex_unlock(&g_lock);

        // 2. 无锁文件 I/O：先写入临时文件
        FILE *f = fopen(DEFAULT_JSON_PATH ".tmp", "w");
        if (f) {
            fprintf(f, "[\n");
            for (int i = 0; i < local_record_idx; i++) {
                ip_record_t *ip = &local_records[i];
                
                fprintf(f, "  {\n");
                fprintf(f, "    \"ip\": \"%s\",\n", ip->ip);
                fprintf(f, "    \"tx_tot\": %" PRIu64 ",\n", ip->total_tx_bytes);
                fprintf(f, "    \"rx_tot\": %" PRIu64 ",\n", ip->total_rx_bytes);
                fprintf(f, "    \"tx_peak\": %" PRIu64 ",\n", ip->tx_peak_rate);
                fprintf(f, "    \"rx_peak\": %" PRIu64 ",\n", ip->rx_peak_rate);
                fprintf(f, "    \"tx_rate\": %" PRIu64 ",\n", ip->tx_current_rate);
                fprintf(f, "    \"rx_rate\": %" PRIu64 ",\n", ip->rx_current_rate);
                fprintf(f, "    \"tx_2s\": %" PRIu64 ",\n", ip->tx_avg_2s);
                fprintf(f, "    \"tx_10s\": %" PRIu64 ",\n", ip->tx_avg_10s);
                fprintf(f, "    \"tx_40s\": %" PRIu64 ",\n", ip->tx_avg_40s);
                fprintf(f, "    \"rx_2s\": %" PRIu64 ",\n", ip->rx_avg_2s);
                fprintf(f, "    \"rx_10s\": %" PRIu64 ",\n", ip->rx_avg_10s);
                fprintf(f, "    \"rx_40s\": %" PRIu64 "\n", ip->rx_avg_40s);
                
                if (i < local_record_idx - 1) {
                    fprintf(f, "  },\n");
                } else {
                    fprintf(f, "  }\n");
                }
            }
            fprintf(f, "]\n"); 
            fclose(f);
            
            // 3. 原子重命名保证前端总是读取到完整的 JSON 文件
            rename(DEFAULT_JSON_PATH ".tmp", DEFAULT_JSON_PATH);
        } else {
            static int warned = 0;
            if (!warned) {
                perror("打开 JSON 文件失败 (将不再重复提示)");
                warned = 1;
            }
        }
    }

    // 优雅清理退出
    printf("NetTrail 正在关机...\n");
    pcap_breakloop(handle);
    pthread_join(tid, NULL);
    pcap_freecode(&fp);
    pcap_close(handle);
    pthread_mutex_destroy(&g_lock);
    
    return 0;
}
