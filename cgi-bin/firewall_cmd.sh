#!/bin/sh

# OpenWRT Firewall Command Script
# 用法: ./firewall_cmd.sh add <protocol> <src_ip> <dest_port> <action>

COMMAND=$1
PROTOCOL=$2
SRC_IP=$3
DEST_PORT=$4
ACTION=$5

# 转换 action，将前端可能传入的小写转换为 UCI 规范的大写，如 ACCEPT, DROP, REJECT
TARGET=$(echo "$ACTION" | tr 'a-z' 'A-Z')

if [ "$COMMAND" = "add" ]; then
    # 生成一个唯一的规则名字，避免冲突
    RULE_NAME="traffic_monitor_$(date +%s)"
    
    # 使用 uci 添加规则
    uci add firewall rule
    uci set firewall.@rule[-1].name="$RULE_NAME"
    # 根据实际情况，限制的源区域一般是 wan (外部) 或者是 lan (内部)
    # 这里默认作为入口限制，设定 src 为 wan
    uci set firewall.@rule[-1].src='wan'
    uci set firewall.@rule[-1].target="$TARGET"
    
    # 如果指定了协议且不为空
    if [ -n "$PROTOCOL" ] && [ "$PROTOCOL" != "any" ]; then
        uci set firewall.@rule[-1].proto="$PROTOCOL"
    fi
    
    # 如果指定了源 IP 且不为空
    if [ -n "$SRC_IP" ] && [ "$SRC_IP" != "any" ]; then
        uci set firewall.@rule[-1].src_ip="$SRC_IP"
    fi
    
    # 如果指定了目标端口且不为空
    if [ -n "$DEST_PORT" ] && [ "$DEST_PORT" != "any" ]; then
        uci set firewall.@rule[-1].dest_port="$DEST_PORT"
    fi

    # 提交配置并重新加载防火墙规则
    uci commit firewall
    /etc/init.d/firewall reload
    
    echo "Rule $RULE_NAME added successfully and firewall reloaded."
    exit 0
elif [ "$COMMAND" = "list" ]; then
    echo "["
    # 获取所有前缀为 traffic_monitor_ 的自定义规则，提取出它们的 uci 标识符 (如 @rule[2])
    RULES=$(uci -q show firewall | grep 'traffic_monitor_' | cut -d'.' -f2 | cut -d'=' -f1 | sort | uniq)
    FIRST=1
    for RULE in $RULES; do
        NAME=$(uci -q get firewall.$RULE.name)
        PROTO=$(uci -q get firewall.$RULE.proto)
        SRC_IP=$(uci -q get firewall.$RULE.src_ip)
        DEST_PORT=$(uci -q get firewall.$RULE.dest_port)
        TARGET=$(uci -q get firewall.$RULE.target)
        
        [ "$FIRST" = 0 ] && echo ","
        FIRST=0
        echo -n "{\"name\":\"$NAME\",\"proto\":\"${PROTO:-any}\",\"src_ip\":\"${SRC_IP:-any}\",\"dest_port\":\"${DEST_PORT:-any}\",\"target\":\"${TARGET:-ACCEPT}\"}"
    done
    echo ""
    echo "]"
    exit 0
else
    echo "Error: Unknown command or missing parameters."
    echo "Usage: $0 add <protocol> <src_ip> <dest_port> <action>"
    echo "       $0 list"
    exit 1
fi
