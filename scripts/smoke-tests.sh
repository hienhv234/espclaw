#!/bin/bash
# ============================================================
# ESPClaw Backend Smoke Tests
# Run this against your staging deployment BEFORE production
# ============================================================

set -e

BASE_URL="${BASE_URL:-http://localhost:3000}"
PASS=0
FAIL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}✓ PASS${NC} $1"; ((PASS++)); }
fail() { echo -e "${RED}✗ FAIL${NC} $1"; ((FAIL++)); }
info() { echo -e "${YELLOW}ℹ INFO${NC} $1"; }

echo "============================================"
echo "ESPClaw Backend Smoke Tests"
echo "Base URL: $BASE_URL"
echo "============================================"
echo ""

# --- Test 1: Health endpoint ---
info "Test 1: GET /api/health"
HEALTH=$(curl -s -w "\n%{http_code}" "$BASE_URL/api/health")
STATUS=$(echo "$HEALTH" | tail -1)
BODY=$(echo "$HEALTH" | head -n -1)
if [ "$STATUS" = "200" ] || [ "$STATUS" = "503" ]; then
  pass "/api/health returned $STATUS"
  echo "$BODY" | jq -r '.checks | to_entries[] | "  \(.key): \(.value)"' 2>/dev/null || true
else
  fail "/api/health returned $STATUS (expected 200 or 503)"
fi
echo ""

# --- Test 2: Health endpoint returns correct structure ---
info "Test 2: Health response structure"
if echo "$BODY" | jq -e '.status, .version, .timestamp, .checks' >/dev/null 2>&1; then
  pass "Health response has correct structure"
else
  fail "Health response missing required fields"
fi
echo ""

# --- Test 3: Device ID validation (invalid format) ---
info "Test 3: Device ID validation — invalid format"
RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL/api/device/request-otp" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"INVALID"}')
STATUS=$(echo "$RESP" | tail -1)
if [ "$STATUS" = "400" ]; then
  pass "Invalid device_id rejected with 400"
else
  fail "Invalid device_id returned $STATUS (expected 400)"
fi
echo ""

# --- Test 4: Device ID validation (valid format, no tenant) ---
info "Test 4: Device ID validation — valid format"
RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL/api/device/request-otp" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"GETAI-A3F2K"}')
STATUS=$(echo "$RESP" | tail -1)
BODY=$(echo "$RESP" | head -n -1)
if [ "$STATUS" = "400" ]; then
  pass "Unknown device without tenant rejected (expected 400)"
else
  info "Device without tenant returned $STATUS (body: $BODY)"
fi
echo ""

# --- Test 5: Rate limiting (too many requests) ---
info "Test 5: Rate limiting — burst 20 requests"
RATE_LIMITED=0
for i in $(seq 1 20); do
  RESP=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE_URL/api/device/request-otp" \
    -H "Content-Type: application/json" \
    -d '{"device_id":"GETAI-TEST1"}')
  if [ "$RESP" = "429" ]; then
    RATE_LIMITED=1
    break
  fi
done
if [ $RATE_LIMITED -eq 1 ]; then
  pass "Rate limiting triggered after burst"
else
  info "Rate limiting not triggered (OK if Upstash not configured)"
fi
echo ""

# --- Test 6: Neuron sync endpoint validation ---
info "Test 6: Neuron sync — invalid tenant_id"
RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL/api/neuron/sync" \
  -H "Content-Type: application/json" \
  -d '{"tenant_id":"not-a-uuid","nodes":[],"links":[],"pulses":[]}')
STATUS=$(echo "$RESP" | tail -1)
if [ "$STATUS" = "400" ]; then
  pass "Invalid tenant_id rejected with 400"
else
  fail "Invalid tenant_id returned $STATUS (expected 400)"
fi
echo ""

# --- Test 7: CORS headers ---
info "Test 7: CORS headers on health endpoint"
CORS=$(curl -s -I "$BASE_URL/api/health" | grep -i "access-control" || true)
if [ -n "$CORS" ]; then
  pass "CORS headers present"
  echo "$CORS" | while read -r line; do echo "  $line"; done
else
  info "No CORS headers (OK if internal endpoint)"
fi
echo ""

# --- Test 8: No server errors on invalid JSON ---
info "Test 8: Invalid JSON body handling"
RESP=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL/api/device/request-otp" \
  -H "Content-Type: application/json" \
  -d '{invalid json}')
STATUS=$(echo "$RESP" | tail -1)
if [ "$STATUS" = "400" ]; then
  pass "Invalid JSON rejected with 400"
else
  fail "Invalid JSON returned $STATUS (expected 400)"
fi
echo ""

# ============================================================
echo "============================================"
echo "Results: $PASS passed, $FAIL failed"
echo "============================================"
if [ $FAIL -gt 0 ]; then
  echo -e "${RED}Some tests failed. Review before deploying to production.${NC}"
  exit 1
else
  echo -e "${GREEN}All tests passed!${NC}"
  exit 0
fi
