#!/bin/bash
# Health check for tbox-someip service.
# Exit 0 = healthy, non-zero = unhealthy.
#
# TBOX-SOMEIP-DSN-CR-006 §11.7: SOMEIP 特有健康检查入口（systemd 启停、
# 唯一实例）。不连接生产 Broker、不使用真实车辆身份/业务数据。

set -euo pipefail

if ! systemctl is-active --quiet tbox-someip.service; then
    echo "FAIL: tbox-someip.service is not active"
    exit 1
fi

# 全系统唯一实例（US-004：不得被其他进程自行 fork）
COUNT=$(pgrep -x tbox-someip | wc -l | tr -d ' ')
if [ "${COUNT}" -ne 1 ]; then
    echo "FAIL: expected exactly 1 tbox-someip process, found ${COUNT}"
    exit 1
fi

echo "OK: tbox-someip.service active (single instance)"
exit 0
