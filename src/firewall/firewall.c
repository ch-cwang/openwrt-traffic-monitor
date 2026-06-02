#include <ctype.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 4096

/**
 * 从 HTTP 请求体 (JSON) 中解析指定字段的值。
 * 支持处理形如 "key": "value" 或 "key":"value" 的数据，自动过滤空格和引号。
 *
 * @param body 输入的 HTTP 请求体内容
 * @param type 需要提取的 JSON 键名 (Key)
 * @param dest 提取到的值将被存入的目标缓冲区
 */
void deal(const char *body, const char *type, char *dest) {
  if (body == NULL || type == NULL || dest == NULL) {
    if (dest)
      dest[0] = '\0';
    return;
  }

  char *idx = strstr(body, type);
  if (idx != NULL) {
    idx += strlen(type);
    // 跳过 JSON 格式中的冒号、引号及空格，如 `"protocol": "tcp"` 或
    // `"protocol":"tcp"`
    while (*idx == '"' || *idx == ':' || *idx == ' ' || *idx == '\t') {
      idx++;
    }
    // 复制值到目标缓冲区，直到遇到控制字符、逗号、右括号、空格或字符串结束符
    while (*idx != '"' && *idx != ',' && *idx != '}' && *idx != '\n' &&
           *idx != '\r' && *idx != ' ' && *idx != '\0') {
      *dest++ = *idx++;
    }
    *dest = '\0';
  } else {
    dest[0] = '\0';
  }
}

/**
 * 校验 IP 地址是否合法。
 * 仅允许符合 IPv4 标准的十进制格式 (e.g., 192.168.1.1)，或允许 "any"
 * 及空值表示任意 IP。
 *
 * @param ip 需要校验的 IP 字符串
 * @return 合法返回 1，非法返回 0
 */
int is_valid_ip(const char *ip) {
  if (ip == NULL)
    return 0;
  if (strcmp(ip, "any") == 0 || strlen(ip) == 0)
    return 1;

  int ip1, ip2, ip3, ip4;
  char tail[16] = {0};
  // 严格限制格式必须为：数字.数字.数字.数字 且后面没有多余字符
  if (sscanf(ip, "%d.%d.%d.%d%s", &ip1, &ip2, &ip3, &ip4, tail) != 4)
    return 0;
  if (strlen(tail) > 0)
    return 0;

  // 校验每个网段的值是否在 0 - 255 之间
  return (ip1 >= 0 && ip1 <= 255) && (ip2 >= 0 && ip2 <= 255) &&
         (ip3 >= 0 && ip3 <= 255) && (ip4 >= 0 && ip4 <= 255);
}

/**
 * 校验目标端口是否合法。
 * 允许纯数字的端口 (1-65535)，或允许 "any" 及空值表示任意端口。
 *
 * @param str 需要校验的端口字符串
 * @return 合法返回 1，非法返回 0
 */
int is_valid_port(const char *str) {
  if (str == NULL || strlen(str) == 0 || strcmp(str, "any") == 0)
    return 1;

  // 确保全部字符均为数字
  for (int i = 0; str[i] != '\0'; i++) {
    if (!isdigit(str[i]))
      return 0;
  }

  int port = atoi(str);
  return port >= 1 && port <= 65535;
}

/**
 * 校验传输协议是否合法，防止恶意注入非预期协议名。
 *
 * @param proto 协议字符串
 * @return 合法返回 1，非法返回 0
 */
int is_valid_protocol(const char *proto) {
  if (proto == NULL || strlen(proto) == 0)
    return 1;
  return (strcasecmp(proto, "tcp") == 0 || strcasecmp(proto, "udp") == 0 ||
          strcasecmp(proto, "tcpudp") == 0 || strcasecmp(proto, "icmp") == 0 ||
          strcasecmp(proto, "any") == 0);
}

/**
 * 校验过滤动作是否合法。
 *
 * @param act 动作字符串 (accept, drop, reject)
 * @return 合法返回 1，非法返回 0
 */
int is_valid_action(const char *act) {
  if (act == NULL || strlen(act) == 0)
    return 1;
  return (strcasecmp(act, "accept") == 0 || strcasecmp(act, "drop") == 0 ||
          strcasecmp(act, "reject") == 0);
}

int main() {
  int server_fd, new_socket;
  struct sockaddr_in address;
  socklen_t addrlen = sizeof(address);
  char buffer[BUFFER_SIZE] = {0};

  // 创建 TCP 套接字
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("Socket creation failed");
    return 1;
  }

  // 设置地址 and 端口复用，防止重启服务器时报 Address already in use
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)) == -1) {
    perror("setsockopt failed");
    close(server_fd);
    return 1;
  }

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  // 绑定端口
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Bind failed");
    close(server_fd);
    return 1;
  }

  // 开始监听请求
  if (listen(server_fd, 3) < 0) {
    perror("Listen failed");
    close(server_fd);
    return 1;
  }

  printf("Firewall control backend server listening on port %d...\n", PORT);

  // 主服务循环：接收并处理前端请求
  while (1) {
    new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (new_socket < 0) {
      perror("Accept failed");
      continue;
    }

    memset(buffer, 0, BUFFER_SIZE);
    ssize_t bytes_read = read(new_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
      close(new_socket);
      continue;
    }

    // 解析 HTTP 请求行，例如 "OPTIONS /api/firewall/add HTTP/1.1" 或 "POST ..."
    char method[16] = {0};
    char path[256] = {0};
    sscanf(buffer, "%15s %255s", method, path);

    // 扩展 buffer 大小以容纳返回的 JSON 列表
    char response[8192] = {0};

    // 1. 处理浏览器的 CORS 预检请求 (OPTIONS)
    if (strcasecmp(method, "OPTIONS") == 0) {
      sprintf(response,
              "HTTP/1.1 204 No Content\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "Connection: close\r\n\r\n");
      write(new_socket, response, strlen(response));
    }
    // 2. 处理 GET 请求 (获取列表)
    else if (strcasecmp(method, "GET") == 0 && strstr(path, "/api/firewall/list") != NULL) {
      char json_body[4096] = {0};
      FILE *fp = popen("/www/cgi-bin/firewall_cmd.sh list", "r");
      if (fp != NULL) {
        fread(json_body, 1, sizeof(json_body) - 1, fp);
        pclose(fp);
      } else {
        strcpy(json_body, "[]");
      }

      sprintf(response,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Connection: close\r\n\r\n"
              "%s", json_body);
      write(new_socket, response, strlen(response));
    }
    // 3. 处理 POST 请求 (添加防火墙规则)
    else if (strcasecmp(method, "POST") == 0 && strstr(path, "/api/firewall/add") != NULL) {
      // 查找 HTTP 协议体起点 (\r\n\r\n 后即为 body)
      char *body = strstr(buffer, "\r\n\r\n");
      if (body != NULL) {
        body += 4;
      } else {
        body = buffer;
      }

      char protocol[16] = {0};
      char src_ip[32] = {0};
      char dest_port[16] = {0};
      char action[16] = {0};

      // 从请求体中解析参数。兼容 "protocol" 和拼写错误的 "protocal"
      deal(body, "protocol", protocol);
      if (strlen(protocol) == 0) {
        deal(body, "protocal", protocol);
      }
      deal(body, "src_ip", src_ip);
      deal(body, "dest_port", dest_port);
      deal(body, "action", action);

      // 【安全校验关卡】：深度限制参数合法性，严防命令注入
      if (!is_valid_ip(src_ip)) {
        sprintf(response,
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n"
                "{\"success\":false,\"message\":\"C后端防御: IP地址格式不合规!\"}");
        write(new_socket, response, strlen(response));
      } else if (!is_valid_port(dest_port)) {
        sprintf(response,
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n"
                "{\"success\":false,\"message\":\"C后端防御: 端口必须在 1-65535 之间或为 any/空!\"}");
        write(new_socket, response, strlen(response));
      } else if (!is_valid_protocol(protocol)) {
        sprintf(response,
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n"
                "{\"success\":false,\"message\":\"C后端防御: 协议类型不受支持!\"}");
        write(new_socket, response, strlen(response));
      } else if (!is_valid_action(action)) {
        sprintf(response,
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n\r\n"
                "{\"success\":false,\"message\":\"C后端防御: 过滤动作无效!\"}");
        write(new_socket, response, strlen(response));
      } else {
        // 安全校验全部通过！组装安全指令
        char cmd[512] = {0};
        snprintf(cmd, sizeof(cmd), "/www/cgi-bin/firewall_cmd.sh add %s %s %s %s",
                 strlen(protocol) > 0 ? protocol : "any",
                 strlen(src_ip) > 0 ? src_ip : "any",
                 strlen(dest_port) > 0 ? dest_port : "any",
                 strlen(action) > 0 ? action : "reject");

        printf("Executing command: %s\n", cmd);

        // 调用系统底层 Shell 脚本修改 uci 防火墙配置
        int ret = system(cmd);

        if (ret == 0) {
          sprintf(response,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: close\r\n\r\n"
                  "{\"success\":true,\"message\":\"C后端通知: 底层防火墙规则应用成功！\"}");
        } else {
          sprintf(response,
                  "HTTP/1.1 500 Internal Server Error\r\n"
                  "Content-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n"
                  "Connection: close\r\n\r\n"
                  "{\"success\":false,\"message\":\"C后端警告: 底层脚本执行失败！\"}");
        }
        write(new_socket, response, strlen(response));
      }
    }
    // 4. 处理其他请求
    else {
      sprintf(response,
              "HTTP/1.1 404 Not Found\r\n"
              "Content-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Connection: close\r\n\r\n"
              "{\"success\":false,\"message\":\"Not Found\"}");
      write(new_socket, response, strlen(response));
    }

    close(new_socket);
  }

  close(server_fd);
  return 0;
}