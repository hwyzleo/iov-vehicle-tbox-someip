#!/usr/bin/env python3
"""
TBOX-SOMEIP 配置校验脚本（CR-007 §8 CI 门禁 A–F / CR-008）

校验内容：
  A. someip.default.yaml / BUILD overlay -> schema/pass（结构、值、有界、未知键）
  B. vsomeip.json -> 寻址与 someip.yaml 一致
  C. types.h 基线 -> 寻址一致；旧 0x0001/0x8001 以 reserved_ids 形式存在（CR-008）
  D. 禁止内容扫描（identity/secret/payload/mock/fixture/bypass/HOST/build path/
     旧 FOTA 成员名 / vehicle.ota / 独立 payload_bytes）
  E. dev 差集（dev 配置不含 production 禁止键；production 不含 dev-only 键）
  F. CR-008：reserved 0x0001/0x8001 零注册；generic_message 有界；
     tsp_timeout_ms < request_deadline_ms

用法：
  python3 scripts/validate_config.py \
      --config config/someip.default.yaml \
      --vsomeip config/runtime/vsomeip.json \
      --types include/tbox_someip/types.h \
      [--dev]            # dev profile：允许 enable_*，成员 test-only ID 必需
  退出码 0 = pass，1 = fail
"""
import argparse
import json
import os
import re
import sys

try:
    import yaml
except ImportError:
    sys.stderr.write("ERROR: PyYAML required (pip install pyyaml)\n")
    sys.exit(2)

# ---- Registry 基线（CR-007 §8 gate D 一致性基准；CR-008 无未批准成员 ID）----
BASELINE = {
    "service_id": 0x6101,
    "instance_id": 0x0001,
    "transport": "TCP",
    "port": 56101,
    "sd_protocol": "UDP",
    "sd_port": 30490,
}

# ---- CR-008 reserved：旧 FOTA 成员 ID 永久保留，不得注册/复用 ----
RESERVED_IDS = {"method": [0x0001], "event": [0x8001]}
RESERVED_MEMBER_NAMES = ("report_software_inventory", "fota_command")
FORBIDDEN_SERVICES = ("vehicle.ota",)

# ---- production 允许的键（未知键拒绝）----
ALLOWED_PRODUCTION_KEYS = {
    "someip": {
        "config_version", "service", "service_discovery", "members",
        "generic_message", "bridge", "clients", "log",
    },
    "members": {"vehicle_message_exchange", "vehicle_message_downlink"},
    "vehicle_message_exchange": {"type", "id", "interface_major"},
    "vehicle_message_downlink": {"type", "id", "eventgroup_id"},
    "generic_message": {"allowed_services", "allowed_protocol_majors",
                        "max_envelope_bytes", "max_payload_bytes", "max_in_flight",
                        "downlink_queue_capacity", "worker_count", "tsp_timeout_ms"},
}
DEV_ONLY_KEYS = {"someip.enable_net_status", "someip.enable_remote_cmd_relay",
                 "someip.enable_vehicle_message_gateway"}

# ---- 禁止内容（CR-007 §8 REQ-008 / CR-008：解析后扫描键名/值，不扫注释）----
FORBIDDEN_KEYS = {
    "vin", "device_sn", "device-sn", "ecu_uid", "sn", "token", "password",
    "passwd", "private_key", "private-key", "client_cert", "root_ca",
    "secret", "credential", "raw_payload", "fota_payload", "someip_payload",
    "mock", "fixture", "bypass", "test_peer", "fake",
}
FORBIDDEN_VALUE_TOKENS = [
    "/tmp/tbox-someip.sock", "/usr/local", "/Users", "/home/", "../",
    "CMakeFiles",
]
# CR-008：旧 FOTA 成员名 / 旧服务别名 / 独立 payload_bytes / 旧符号
FORBIDDEN_KEY_TOKENS = ["report_software_inventory", "fota_command"]
FORBIDDEN_VALUE_SERVICE_TOKENS = [
    "vehicle.ota", "vehicle.ota.v1", "payload_bytes",
    "reportSoftwareInventory", "onFotaCommand",
]

errors = []


def err(msg):
    errors.append(msg)


def parse_int(v):
    """解析 int 或 '0xXXXX' 字符串为 int。"""
    if isinstance(v, int):
        return v
    if isinstance(v, str):
        return int(v, 0)
    raise ValueError(f"cannot parse int from {v!r}")


# ============================================================
# A. schema 校验
# ============================================================
def check_schema(cfg, dev_mode):
    s = cfg.get("someip")
    if not isinstance(s, dict):
        err("A: missing someip section")
        return

    # SOMEIP 是 consumer/bridge，不提供 IPC Server（CR-007 §6）
    if "ipc" in s:
        err("A: someip.ipc forbidden (SOMEIP is consumer/bridge, no IPC Server)")

    # 未知键（production）
    if not dev_mode:
        for k in s:
            if k not in ALLOWED_PRODUCTION_KEYS["someip"]:
                err(f"A: unknown production key someip.{k}")
        for k in ("enable_net_status", "enable_remote_cmd_relay", "enable_vehicle_message_gateway"):
            if k in s:
                err(f"A: dev-only key someip.{k} forbidden in production")

    # config_version
    if s.get("config_version") != 1:
        err(f"A: someip.config_version must be 1, got {s.get('config_version')}")

    # service
    svc = s.get("service", {})
    _check_val("someip.service.service_id", svc.get("service_id"), BASELINE["service_id"], parse_int)
    _check_val("someip.service.instance_id", svc.get("instance_id"), BASELINE["instance_id"], parse_int)
    _check_val("someip.service.transport", svc.get("transport"), BASELINE["transport"])
    _check_val("someip.service.port", svc.get("port"), BASELINE["port"], parse_int)

    # service_discovery
    sd = s.get("service_discovery", {})
    _check_val("someip.service_discovery.protocol", sd.get("protocol"), BASELINE["sd_protocol"])
    _check_val("someip.service_discovery.port", sd.get("port"), BASELINE["sd_port"], parse_int)
    _check_bounded("someip.service_discovery.initial_delay_ms", sd.get("initial_delay_ms"), 0, 60000)
    _check_bounded("someip.service_discovery.repetition_base_delay_ms",
                   sd.get("repetition_base_delay_ms"), 0, 60000)

    # members（CR-008：仅通用车云消息成员；旧成员名 reserved 拒绝）
    members = s.get("members", {})
    for old in RESERVED_MEMBER_NAMES:
        if old in members:
            err(f"A: reserved old member {old!r} must not be configured (CR-008)")

    ex = members.get("vehicle_message_exchange", {})
    _check_val("someip.members.vehicle_message_exchange.type", ex.get("type"), "method")
    _check_member_id("someip.members.vehicle_message_exchange.id", ex.get("id"),
                     "method", dev_mode)
    if ex.get("interface_major") is not None:
        _check_bounded("someip.members.vehicle_message_exchange.interface_major",
                       ex.get("interface_major"), 1, 255)

    dl = members.get("vehicle_message_downlink", {})
    _check_val("someip.members.vehicle_message_downlink.type", dl.get("type"), "event")
    _check_member_id("someip.members.vehicle_message_downlink.id", dl.get("id"),
                     "event", dev_mode)
    if dl.get("eventgroup_id") is not None:
        _check_bounded("someip.members.vehicle_message_downlink.eventgroup_id",
                       dl.get("eventgroup_id"), 0, 65535)

    # generic_message（CR-008）
    gm = s.get("generic_message", {})
    _check_list_str("someip.generic_message.allowed_services",
                    gm.get("allowed_services"), ["vehicle.fota"])
    for svc in (gm.get("allowed_services") or []):
        if svc in FORBIDDEN_SERVICES:
            err(f"A: forbidden service alias {svc!r} (CR-008 no vehicle.ota fallback)")
    _check_list_int("someip.generic_message.allowed_protocol_majors",
                    gm.get("allowed_protocol_majors"), [1])
    _check_bounded("someip.generic_message.max_envelope_bytes", gm.get("max_envelope_bytes"), 1, 1048576)
    _check_bounded("someip.generic_message.max_payload_bytes", gm.get("max_payload_bytes"), 1, 1048576)
    _check_bounded("someip.generic_message.max_in_flight", gm.get("max_in_flight"), 1, 1024)
    _check_bounded("someip.generic_message.downlink_queue_capacity",
                   gm.get("downlink_queue_capacity"), 1, 65536)
    _check_bounded("someip.generic_message.worker_count", gm.get("worker_count"), 1, 16)
    _check_bounded("someip.generic_message.tsp_timeout_ms", gm.get("tsp_timeout_ms"), 100, 30000)

    # bridge + tsp_timeout < request_deadline（CR-008 §配置增量）
    br = s.get("bridge", {})
    _check_bounded("someip.bridge.request_deadline_ms", br.get("request_deadline_ms"), 100, 30000)
    _check_bounded("someip.bridge.queue_capacity", br.get("queue_capacity"), 1, 4096)
    _check_bounded("someip.bridge.worker_count", br.get("worker_count"), 1, 16)
    if gm.get("tsp_timeout_ms") is not None and br.get("request_deadline_ms") is not None:
        try:
            if parse_int(gm["tsp_timeout_ms"]) >= parse_int(br["request_deadline_ms"]):
                err("A: generic_message.tsp_timeout_ms must be < bridge.request_deadline_ms (CR-008)")
        except ValueError:
            err("A: tsp_timeout_ms / request_deadline_ms not int")

    # clients
    clients = s.get("clients", {})
    for name in ("tsp", "prov"):
        c = clients.get(name, {})
        sp = c.get("socket_path", "")
        if not re.match(r"^/tmp/tbox-(tsp|prov)\.sock$", sp):
            err(f"A: someip.clients.{name}.socket_path invalid: {sp!r}")
        _check_bounded(f"someip.clients.{name}.connect_timeout_ms",
                       c.get("connect_timeout_ms"), 100, 30000)


def _check_val(key, actual, expected, conv=None):
    if actual is None:
        err(f"A: {key} missing")
        return
    a = conv(actual) if conv else actual
    if a != expected:
        err(f"A: {key} expected {expected:#x} ({expected}), got {actual!r}")


def _check_member_id(key, actual, member_type, dev_mode):
    """CR-008：成员 ID 必须在类型范围且不得为 reserved。
    production 默认可缺省（pending Registry）；dev 模式必须显式 test-only ID。"""
    if actual is None:
        if dev_mode:
            err(f"A: {key} required in dev profile (test-only ID)")
        return  # production default: pending Registry assignment
    try:
        v = parse_int(actual)
    except ValueError:
        err(f"A: {key} not int: {actual!r}")
        return
    lo, hi = (0x0001, 0x7FFF) if member_type == "method" else (0x8000, 0xFFFF)
    if not (lo <= v <= hi):
        err(f"A: {key} out of {member_type} range [{lo:#x},{hi:#x}]: {v}")
    if v in RESERVED_IDS[member_type]:
        err(f"A: {key} uses reserved ID {v:#x} (CR-008 zero-registration)")


def _check_bounded(key, actual, lo, hi):
    if actual is None:
        err(f"A: {key} missing")
        return
    try:
        v = parse_int(actual)
    except Exception:
        err(f"A: {key} not int: {actual!r}")
        return
    if v < lo or v > hi:
        err(f"A: {key} out of bounds [{lo},{hi}]: {v}")


def _check_list_str(key, actual, expected):
    if actual is None:
        err(f"A: {key} missing")
        return
    if not isinstance(actual, list) or not actual:
        err(f"A: {key} must be a non-empty list")
        return
    if set(actual) != set(expected):
        err(f"A: {key} expected {expected}, got {actual!r}")


def _check_list_int(key, actual, expected):
    if actual is None:
        err(f"A: {key} missing")
        return
    if not isinstance(actual, list) or not actual:
        err(f"A: {key} must be a non-empty list")
        return
    try:
        got = [parse_int(x) for x in actual]
    except ValueError:
        err(f"A: {key} not int list: {actual!r}")
        return
    if got != expected:
        err(f"A: {key} expected {expected}, got {actual!r}")


# ============================================================
# B. vsomeip.json 一致性
# ============================================================
def check_vsomeip(vj, cfg):
    s = cfg.get("someip", {}).get("service", {})
    sd = cfg.get("someip", {}).get("service_discovery", {})

    apps = vj.get("applications", [])
    if not apps or parse_int(apps[0].get("id")) != BASELINE["service_id"]:
        err(f"B: vsomeip applications[0].id != {BASELINE['service_id']:#x}")

    svcs = vj.get("services", [])
    if not svcs:
        err("B: vsomeip services empty")
    else:
        if parse_int(svcs[0].get("service")) != BASELINE["service_id"]:
            err(f"B: vsomeip services[0].service != {BASELINE['service_id']:#x}")
        if parse_int(svcs[0].get("instance")) != BASELINE["instance_id"]:
            err(f"B: vsomeip services[0].instance != {BASELINE['instance_id']:#x}")
        if parse_int(svcs[0].get("reliable")) != BASELINE["port"]:
            err(f"B: vsomeip services[0].reliable != {BASELINE['port']}")

    vsd = vj.get("service-discovery", {})
    if parse_int(vsd.get("port")) != BASELINE["sd_port"]:
        err(f"B: vsomeip service-discovery.port != {BASELINE['sd_port']}")


# ============================================================
# C. types.h 基线一致性（CR-008：寻址一致；旧 ID 以 reserved 形式存在）
# ============================================================
def check_types(types_path):
    if not os.path.isfile(types_path):
        err(f"C: types.h not found: {types_path}")
        return
    text = open(types_path).read()
    expected = {
        "SERVICE_ID": BASELINE["service_id"],
        "INSTANCE_ID": BASELINE["instance_id"],
        "TCP_PORT": BASELINE["port"],
        "SD_PORT": BASELINE["sd_port"],
        # CR-008：旧 FOTA 成员 ID 以 reserved_ids 形式保留，不得删除/复用
        "REPORT_SOFTWARE_INVENTORY": RESERVED_IDS["method"][0],
        "FOTA_COMMAND": RESERVED_IDS["event"][0],
    }
    for name, exp in expected.items():
        m = re.search(rf"constexpr\s+uint16_t\s+{name}\s*=\s*(0x[0-9A-Fa-f]+|\d+)", text)
        if not m:
            err(f"C: types.h {name} not found")
            continue
        if parse_int(m.group(1)) != exp:
            err(f"C: types.h {name} expected {exp:#x}, got {m.group(1)}")


# ============================================================
# D. 禁止内容扫描
# ============================================================
def _collect_keys_values(data, keys, values):
    if isinstance(data, dict):
        for k, v in data.items():
            keys.add(str(k).lower())
            if not isinstance(v, (dict, list)):
                values.append(str(v))
            _collect_keys_values(v, keys, values)
    elif isinstance(data, list):
        for item in data:
            _collect_keys_values(item, keys, values)


def check_forbidden(data, path):
    if data is None:
        err(f"D: cannot parse {path}")
        return
    keys, values = set(), []
    _collect_keys_values(data, keys, values)
    for k in keys:
        if k in FORBIDDEN_KEYS:
            err(f"D: forbidden key {k!r} in {path}")
        for tok in FORBIDDEN_KEY_TOKENS:
            if tok in k:
                err(f"D: forbidden legacy key token {tok!r} in {path} ({k!r})")
    for v in values:
        vl = v.lower()
        for tok in FORBIDDEN_VALUE_TOKENS:
            if tok.lower() in vl:
                err(f"D: forbidden value token {tok!r} in {path} ({v!r})")
        for tok in FORBIDDEN_VALUE_SERVICE_TOKENS:
            if tok.lower() in vl:
                err(f"D: forbidden legacy service/asset token {tok!r} in {path} ({v!r})")


# ============================================================
# E. dev 差集
# ============================================================
def check_dev_diff(dev_cfg_path):
    if not os.path.isfile(dev_cfg_path):
        return  # dev 配置可选
    with open(dev_cfg_path) as f:
        dev = yaml.safe_load(f)
    check_forbidden(dev, dev_cfg_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", required=True, help="someip.yaml (default/overlay/staging)")
    ap.add_argument("--vsomeip", required=True, help="vsomeip.json")
    ap.add_argument("--types", default="include/tbox_someip/types.h", help="types.h path")
    ap.add_argument("--dev", action="store_true", help="dev profile (allow enable_*)")
    ap.add_argument("--dev-config", default="config/dev/someip.yaml", help="dev someip.yaml")
    args = ap.parse_args()

    with open(args.config) as f:
        cfg = yaml.safe_load(f)
    with open(args.vsomeip) as f:
        vj = json.load(f)

    check_schema(cfg, args.dev)
    check_vsomeip(vj, cfg)
    check_types(args.types)
    check_forbidden(cfg, args.config)
    check_forbidden(vj, args.vsomeip)
    check_dev_diff(args.dev_config)

    if errors:
        for e in errors:
            sys.stderr.write(f"FAIL: {e}\n")
        sys.exit(1)
    print("OK: config validation passed")
    sys.exit(0)


if __name__ == "__main__":
    main()
