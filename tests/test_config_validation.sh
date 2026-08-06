#!/bin/bash
# CR-007 §8 配置校验正反例测试（CI gates A–E）
# 正例：someip.default.yaml / dev someip.yaml -> pass
# 反例：未知键 / 错误 service_id / someip.ipc / 越界参数 -> reject
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VALIDATE="python3 $ROOT/scripts/validate_config.py"
DEFAULT="$ROOT/config/someip.default.yaml"
VSOMEIP="$ROOT/config/runtime/vsomeip.json"
TYPES="$ROOT/include/tbox_someip/types.h"
DEV="$ROOT/config/dev/someip.yaml"

pass=0; fail=0
check_pass() {
  if $VALIDATE --config "$1" --vsomeip "$VSOMEIP" --types "$TYPES" "${@:2}" >/dev/null 2>&1; then
    pass=$((pass+1))
  else
    echo "FAIL (expected pass): $*"; fail=$((fail+1))
  fi
}
check_fail() {
  if $VALIDATE --config "$1" --vsomeip "$VSOMEIP" --types "$TYPES" "${@:2}" >/dev/null 2>&1; then
    echo "FAIL (expected reject): $*"; fail=$((fail+1))
  else
    pass=$((pass+1))
  fi
}

# ---- 正例 ----
check_pass "$DEFAULT"
check_pass "$DEV" --dev

# ---- 反例：未知键（production 拒绝）----
tmp=$(mktemp /tmp/someip_bad_XXXXXX.yaml)
python3 -c "
import yaml
cfg = yaml.safe_load(open('$DEFAULT'))
cfg['someip']['unknown_key'] = 'x'
yaml.safe_dump(cfg, open('$tmp','w'))
"
check_fail "$tmp" "unknown key"

# ---- 反例：错误 service_id ----
python3 -c "
import yaml
cfg = yaml.safe_load(open('$DEFAULT'))
cfg['someip']['service']['service_id'] = 0x6102
yaml.safe_dump(cfg, open('$tmp','w'))
"
check_fail "$tmp" "wrong service_id"

# ---- 反例：someip.ipc 禁止（consumer/bridge 无 IPC Server）----
python3 -c "
import yaml
cfg = yaml.safe_load(open('$DEFAULT'))
cfg['someip']['ipc'] = {'socket_path': '/tmp/tbox-someip.sock'}
yaml.safe_dump(cfg, open('$tmp','w'))
"
check_fail "$tmp" "someip.ipc forbidden"

# ---- 反例：越界 bridge 参数 ----
python3 -c "
import yaml
cfg = yaml.safe_load(open('$DEFAULT'))
cfg['someip']['bridge']['request_deadline_ms'] = 999999
yaml.safe_dump(cfg, open('$tmp','w'))
"
check_fail "$tmp" "bridge out of bounds"

# ---- 反例：production 含 dev-only enable_* ----
python3 -c "
import yaml
cfg = yaml.safe_load(open('$DEFAULT'))
cfg['someip']['enable_net_status'] = False
yaml.safe_dump(cfg, open('$tmp','w'))
"
check_fail "$tmp" "dev-only key in production"

# ---- 反例：vsomeip.json service 不一致 ----
bad_json=$(mktemp /tmp/someip_bad_XXXXXX.json)
python3 -c "
import json
vj = json.load(open('$VSOMEIP'))
vj['services'][0]['service'] = '0x6102'
json.dump(vj, open('$bad_json','w'))
"
if $VALIDATE --config "$DEFAULT" --vsomeip "$bad_json" --types "$TYPES" >/dev/null 2>&1; then
  echo "FAIL (expected reject): vsomeip service mismatch"; fail=$((fail+1))
else
  pass=$((pass+1))
fi

rm -f "$tmp" "$bad_json"

echo "config validation: $pass passed, $fail failed"
[ "$fail" -eq 0 ] || exit 1
exit 0
