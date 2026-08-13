#!/bin/bash
# SOME/IP-SD / 通用车云消息桥接最小 smoke test for tbox-someip service.
# Exit 0 = pass, non-zero = fail.
#
# TBOX-SOMEIP-DSN-CR-006 §11.7 / CR-008: Orin 最小桥接验证入口（本地生命周期 + 配置
# 安全检查 + TCP 56101 端点 + SD UDP 30490 探测）。完整 SD/Method/Event、通用
# VehicleMessage 上下行、PROV client 与异常恢复由受控 Orin 集成用例承担；本脚本使用
# 隔离 peer/测试数据，不连接生产 Broker、不使用真实车辆身份或业务数据。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- 配置存在性 ---
CONFIG_FILE="/etc/tbox/conf.d/someip.yaml"
if [ ! -f "${CONFIG_FILE}" ]; then
    echo "FAIL: config file ${CONFIG_FILE} not found"
    exit 1
fi

# --- 默认配置不得含生产敏感字段/身份实例值（CR-006 §10） ---
if grep -qE "device-sn|ecu_uid|vin:|token|private_key|client_cert|root_ca|secret" "${CONFIG_FILE}"; then
    echo "FAIL: default config contains forbidden identity/credential fields"
    exit 1
fi

# --- 服务生命周期：重启并验证唯一实例 ---
if ! systemctl restart tbox-someip.service; then
    echo "FAIL: could not restart tbox-someip.service"
    exit 1
fi
sleep 2

COUNT=$(pgrep -x tbox_someip | wc -l | tr -d ' ')
if [ "${COUNT}" -ne 1 ]; then
    echo "FAIL: expected exactly 1 tbox_someip process, found ${COUNT}"
    exit 1
fi

# --- TCP 56101 业务端点探测（Service 0x6101 / Instance 0x0001 / TCP 56101） ---
if ! python3 "${SCRIPT_DIR}/../../scripts/test_someip_connection.py" 127.0.0.1 56101; then
    echo "FAIL: TCP 56101 SOME/IP endpoint check failed"
    exit 1
fi

# --- SD UDP 30490 探测（SOME/IP-SD；mock 模式未开启 SD 端口时 SKIP） ---
if command -v ss >/dev/null 2>&1 && ss -lun 2>/dev/null | grep -q ':30490'; then
    echo "OK: SD UDP 30490 listening"
else
    echo "SKIP: SD UDP 30490 not listening (mock mode or SD not yet offered)"
fi

echo "OK: someip smoke passed"
exit 0
