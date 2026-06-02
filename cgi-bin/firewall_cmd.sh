#!/bin/sh

# =================================================================================
# OpenWRT Firewall Configuration Script (traffic_monitor)
# 
# Usage:
#   Add Rule:  ./firewall_cmd.sh add <protocol> <src_ip> <dest_port> <action>
#   List Rules: ./firewall_cmd.sh list
# =================================================================================

COMMAND=$1
PROTOCOL=$2
SRC_IP=$3
DEST_PORT=$4
ACTION=$5

# 将前端传入的动作（action）统一转换为 UCI 规范的大写形式（如 ACCEPT, DROP, REJECT）
TARGET=$(echo "$ACTION" | tr 'a-z' 'A-Z')

if [ "$COMMAND" = "add" ]; then
    # 基于时间戳生成唯一的防火墙规则名称，防止命名冲突
    RULE_NAME="traffic_monitor_$(date +%s)"

    # =============================================================================
    # [1] 规则查重与清理机制
    # 防止由于重复操作导致同一控制条件下不断累加重复的防火墙规则
    # =============================================================================
    
    P_PROTO=$(echo "${PROTOCOL:-any}" | tr 'A-Z' 'a-z')
    P_SRC_IP="${SRC_IP:-any}"
    P_DEST_PORT="${DEST_PORT:-any}"

    # 提取所有由本系统创建的规则索引号，并进行降序排序 (sort -nr)
    # 降序操作至关重要：在遍历删除 UCI 匿名节点时，从大到小删除能避免底层索引移位导致的误删或漏删
    IDXS=$(uci -q show firewall | grep 'traffic_monitor_' | grep '\.name=' | cut -d'.' -f2 | cut -d'[' -f2 | cut -d']' -f1 | sort -nr)
    
    for IDX in $IDXS; do
        RULE="@rule[$IDX]"
        R_PROTO=$(uci -q get firewall.$RULE.proto | tr 'A-Z' 'a-z')
        R_SRC_IP=$(uci -q get firewall.$RULE.src_ip)
        R_DEST_PORT=$(uci -q get firewall.$RULE.dest_port)
        
        # 统一默认值，便于逻辑比对
        [ -z "$R_PROTO" ] && R_PROTO="any"
        [ -z "$R_SRC_IP" ] && R_SRC_IP="any"
        [ -z "$R_DEST_PORT" ] && R_DEST_PORT="any"
        
        # 匹配条件：协议、源IP、目标端口完全一致
        if [ "$R_PROTO" = "$P_PROTO" ] && [ "$R_SRC_IP" = "$P_SRC_IP" ] && [ "$R_DEST_PORT" = "$P_DEST_PORT" ]; then
            # 发现重复规则，执行删除以实现类似 Upsert (覆盖更新) 的效果
            uci delete firewall.$RULE
        fi
    done
    
    # =============================================================================
    # [2] 下发新的防火墙规则
    # =============================================================================
    
    uci add firewall rule
    uci set firewall.@rule[-1].name="$RULE_NAME"
    
    # 设定限制的来源区域 (Zone)
    # 注意：此处限制来源为 wan 区域。如果需要限制宿主机 (lan区域) 访问，应将其修改为 '*' 或 'lan'
    uci set firewall.@rule[-1].src='wan'
    uci set firewall.@rule[-1].target="$TARGET"
    
    # 按需设置协议参数
    if [ -n "$PROTOCOL" ] && [ "$PROTOCOL" != "any" ]; then
        uci set firewall.@rule[-1].proto="$PROTOCOL"
    fi
    
    # 按需设置源 IP 参数
    if [ -n "$SRC_IP" ] && [ "$SRC_IP" != "any" ]; then
        uci set firewall.@rule[-1].src_ip="$SRC_IP"
    fi
    
    # 按需设置目标端口参数
    if [ -n "$DEST_PORT" ] && [ "$DEST_PORT" != "any" ]; then
        uci set firewall.@rule[-1].dest_port="$DEST_PORT"
    fi

    # 提交修改并重载防火墙服务使配置生效
    if uci commit firewall && /etc/init.d/firewall reload >/dev/null 2>&1; then
        echo "Rule $RULE_NAME added successfully and firewall reloaded."
        exit 0
    else
        echo "Error: Failed to apply firewall settings."
        exit 1
    fi

elif [ "$COMMAND" = "list" ]; then
    echo "["
    
    # 过滤获取由本模块管理的所有自定义规则
    RULES=$(uci -q show firewall | grep 'traffic_monitor_' | cut -d'.' -f2 | cut -d'=' -f1 | sort | uniq)
    FIRST=1
    
    for RULE in $RULES; do
        NAME=$(uci -q get firewall.$RULE.name)
        PROTO=$(uci -q get firewall.$RULE.proto)
        SRC_IP=$(uci -q get firewall.$RULE.src_ip)
        DEST_PORT=$(uci -q get firewall.$RULE.dest_port)
        TARGET=$(uci -q get firewall.$RULE.target)
        
        # 处理 JSON 数组的逗号分隔
        [ "$FIRST" = 0 ] && echo ","
        FIRST=0
        
        echo -n "{\"name\":\"$NAME\",\"proto\":\"${PROTO:-any}\",\"src_ip\":\"${SRC_IP:-any}\",\"dest_port\":\"${DEST_PORT:-any}\",\"target\":\"${TARGET:-ACCEPT}\"}"
    done
    
    echo ""
    echo "]"
    exit 0
    
else
    # 异常或非法指令提示
    echo "Error: Unknown command or missing parameters."
    echo "Usage: $0 add <protocol> <src_ip> <dest_port> <action>"
    echo "       $0 list"
    exit 1
fi
