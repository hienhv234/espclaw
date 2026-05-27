# ESPClaw — Backend Deployment Guide

> **Phiên bản:** 2.0 | **Ngày:** 29/04/2026
> **Target:** Production-ready Backend for ESPClaw Edge AI Agent
>
> **Changelog v2.0:**
> - MQTT singleton pool với TLS xác thực
> - RLS tenant-based isolation (32 policies)
> - Zod-style validation (pure TypeScript, no deps)
> - Pino structured logging
> - Rate limiting với Upstash Redis fallback
> - `/api/neuron/sync` + `/api/neuron/pulse` endpoints mới
> - `/api/health` endpoint
> - Lua execution endpoint
> - `FULL_RESET.sql` migration đã chạy (10 tables, 32 policies)

---

## Tổng quan

Tài liệu này hướng dẫn triển khai toàn bộ backend cho hệ thống ESPClaw, bao gồm Supabase, Next.js API, MQTT, và các Edge Functions.

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        INTERNET                                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│  │ Telegram │  │ Dashboard │  │ REST API │  │ ESP32    │      │
│  │   Bot    │  │  (User)   │  │ (Apps)   │  │ Devices  │      │
│  └────┬─────┘  └─────┬────┘  └────┬─────┘  └────┬─────┘      │
└───────┼──────────────┼───────────┼───────────┼───────────────┘
        │              │           │           │
        ▼              ▼           ▼           ▼
┌─────────────────────────────────────────────────────────────────┐
│                     VERCEL EDGE                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              Next.js API Routes (Edge Functions)           │  │
│  │  /api/device/* · /api/neuron/* · /api/lua/*           │  │
│  │  Rate Limiting · Auth JWT · Tenant Validation          │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
        │                  │              │
        ▼                  ▼              ▼
┌────────────────┐ ┌────────────────┐ ┌────────────────┐
│    SUPABASE     │ │  MQTT BROKER   │ │   UPSTASH      │
│   PostgreSQL    │ │  HiveMQ Cloud │ │    Redis       │
│  Auth + Realtime│ │   (TLS 8883)  │ │  Rate Limiting │
│  Storage        │ │                │ │  Cache         │
└────────┬───────┘ └───────┬────────┘ └────────────────┘
         │                   │
         ▼                   ▼
  ┌─────────────────┐ ┌──────────────────┐
  │ Supabase Edge  │ │  ESP32 Firmware  │
  │ Functions       │ │  (Lua Runtime +  │
  │ - detect-patterns│ │   NeuronLink)  │
  │ - ai-insights   │ │                  │
  │ - auto-link     │ └──────────────────┘
  └─────────────────┘
```

---

## 1. Prerequisites

### 1.1 Required Accounts

| Service | Required For | Sign Up |
|---------|------------|---------|
| Supabase | Database, Auth, Storage, Realtime | https://supabase.com |
| HiveMQ Cloud | MQTT Broker | https://www.hivemq.com/cloud |
| Vercel | API Routes, Frontend | https://vercel.com |
| Upstash | Rate Limiting, Caching | https://upstash.com |
| Sentry | Error Tracking | https://sentry.io |
| Resend | Transactional Email | https://resend.com |

### 1.2 Local Environment

```bash
# Required tools
node --version      # >= 18.0.0
npm --version       # >= 9.0.0
git --version       # >= 2.30.0
python --version    # >= 3.8.0 (for ESP-IDF)

# Optional
docker --version   # >= 20.0.0 (for local Supabase)
```

### 1.3 Clone Repository

```bash
git clone https://github.com/your-org/espclaw.git
cd espclaw
```

### 1.4 Local Development Quick Start

```bash
# 1. Reset database (Supabase Dashboard → SQL Editor → paste FULL_RESET.sql)
#    File: supabase/migrations/FULL_RESET.sql

# 2. Start neutron-web
cd neutron-web
npm install
npm run dev
# → http://localhost:3000

# 3. Test health endpoint
curl http://localhost:3000/api/health

# 4. Test device OTP flow
curl -X POST http://localhost:3000/api/device/request-otp \
  -H "Content-Type: application/json" \
  -d '{"device_id":"GETAI-TEST1"}'

# 5. Smoke tests
bash scripts/smoke-tests.sh
```

---

## 2. Supabase Setup

### 2.1 Create Supabase Project

1. Truy cập https://app.supabase.com
2. Click **New Project**
3. Điền thông tin:
   - **Name:** espclaw-prod
   - **Database Password:** [Generate secure password]
   - **Region:** Chọn gần nhất (Singapore ap-southeast-1)
4. Đợi project tạo (~2 phút)

### 2.2 Get API Keys

Sau khi project được tạo, lấy keys từ **Settings → API**:

```bash
# .env.local
NEXT_PUBLIC_SUPABASE_URL=https://xxxx.supabase.co
NEXT_PUBLIC_SUPABASE_ANON_KEY=eyJhbGc...
SUPABASE_SERVICE_ROLE_KEY=eyJhbGc...
```

### 2.3 Run Database Migrations

**Cách nhanh nhất** — chạy `FULL_RESET.sql` trong Supabase SQL Editor:

1. Mở Supabase Dashboard → SQL Editor → New Query
2. Paste nội dung file `supabase/migrations/FULL_RESET.sql`
3. Nhấn **Run**
4. Kết quả mong đợi: `✅ Database reset complete! Tables: 10, RLS policies: 32`

**Cách Supabase CLI:**

```bash
# Cài đặt Supabase CLI
npm install -g supabase

# Login
supabase login

# Link project
supabase link --project-ref xxxx

# Reset database (drop + migrate in order)
supabase db reset

# Hoặc chạy migration riêng lẻ
supabase db push
```

### 2.4 Configure Extensions

```sql
-- Enable required extensions
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_trgm";
CREATE EXTENSION IF NOT EXISTS "vector";

-- Verify extensions
SELECT extname FROM pg_extension WHERE extname IN ('uuid-ossp', 'pg_trgm', 'vector');
```

### 2.5 Configure Realtime

1. Truy cập **Database → Replication**
2. Enable replication cho các bảng:
   - [x] nodes
   - [x] links
   - [x] pulses
3. Click **Save**

### 2.6 Create Storage Bucket

```sql
-- Tạo bucket cho Lua scripts
INSERT INTO storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
VALUES (
  'lua-scripts',
  'lua-scripts',
  false,
  16384,  -- 16KB max
  ARRAY['text/plain', 'application/octet-stream']
);
```

---

## 3. MQTT Setup (HiveMQ Cloud)

### 3.1 Create HiveMQ Cloud Cluster

1. Truy cập https://console.hivemq.cloud
2. Click **New Cluster**
3. Chọn:
   - **Plan:** Serverless (hoặc Starter)
   - **Region:** eu-west-1 (hoặc gần nhất)
4. Đợi cluster tạo (~3 phút)

### 3.2 Create Access Credentials

1. Truy cập **Access Management → Credentials**
2. Click **Add Credentials**
3. Điền:
   - **Username:** espclaw-backend
   - **Password:** [Generate secure password]
4. Lưu credentials

### 3.3 Get Connection Details

```bash
# Broker URL (TLS)
mqtts://xxxxx.s1.eu.hivemq.cloud:8883

# Credentials
MQTT_USERNAME=espclaw-backend
MQTT_PASSWORD=[your-password]
```

---

## 4. Rate Limiting Setup (Upstash)

### 4.1 Create Redis Database

1. Truy cập https://console.upstash.com
2. Click **Create Database**
3. Chọn:
   - **Type:** Serverless (Global)
   - **Region:** Global
4. Lấy REST URL và Token

### 4.2 Configure Rate Limits

```bash
# .env.local
UPSTASH_REDIS_REST_URL=https://xxx.upstash.io
UPSTASH_REDIS_REST_TOKEN=xxx
```

---

## 5. Deploy Next.js Application

### 5.1 Connect to Vercel

```bash
# Cài đặt Vercel CLI
npm install -g vercel

# Login
vercel login

# Deploy
cd neutron-web
vercel
```

### 5.2 Configure Environment Variables

Trong Vercel Dashboard → Settings → Environment Variables:

```bash
# Supabase
NEXT_PUBLIC_SUPABASE_URL=https://xxxx.supabase.co
NEXT_PUBLIC_SUPABASE_ANON_KEY=eyJhbGc...
SUPABASE_SERVICE_ROLE_KEY=eyJhbGc...
DATABASE_URL=postgresql://postgres:[password]@db.xxxx.supabase.co:5432/postgres

# MQTT
NEXT_PUBLIC_MQTT_BROKER_URL=mqtts://xxxxx.s1.eu.hivemq.cloud:8883
MQTT_USERNAME=espclaw-backend
MQTT_PASSWORD=[your-password]

# Rate Limiting
UPSTASH_REDIS_REST_URL=https://xxx.upstash.io
UPSTASH_REDIS_REST_TOKEN=xxx

# Monitoring
SENTRY_DSN=https://xxx@sentry.io/xxx

# App
NEXT_PUBLIC_APP_URL=https://your-app.vercel.app
```

### 5.3 Deploy Edge Functions

```bash
# Deploy
vercel deploy --prod

# Hoặc redeploy sau khi push code
vercel --prod
```

---

## 6. Supabase Edge Functions Deployment

### 6.1 Install Supabase CLI

```bash
npm install -g supabase
```

### 6.2 Deploy Functions

```bash
cd supabase

# Login
supabase login

# Link project
supabase link --project-ref xxxx

# Deploy all functions
supabase functions deploy detect-patterns
supabase functions deploy ai-insights
supabase functions deploy auto-link

# Set secrets
supabase secrets set ANTHROPIC_API_KEY=sk-ant-...
```

### 6.3 Verify Deployment

```bash
# Test function
curl -X POST https://xxxx.supabase.co/functions/v1/detect-patterns \
  -H "Authorization: Bearer [SERVICE_ROLE_KEY]" \
  -H "Content-Type: application/json" \
  -d '{"tenant_id": "test-id", "lookback_hours": 24}'
```

---

## 7. Database Maintenance

### 7.1 Set Up Backups

Trong Supabase Dashboard → Settings → Database:

1. Enable **Point-in-Time Recovery**
2. Configure retention (7-30 days)
3. Test backup restore

### 7.2 Configure Connection Pool

```sql
-- Kiểm tra connection settings
SHOW max_connections;

-- Tối ưu cho Next.js (connection pooler)
-- Settings → Connection Pooling
-- Pool mode: Transaction
-- Pool size: đề xuất 10 cho free tier
```

### 7.3 Index Maintenance

```sql
-- Check index usage
SELECT
  schemaname,
  tablename,
  indexname,
  idx_scan,
  idx_tup_read,
  idx_tup_fetch
FROM pg_stat_user_indexes
ORDER BY idx_scan ASC;

-- REINDEX bảng lớn (chạy off-peak)
REINDEX TABLE CONCURRENTLY nodes;
REINDEX TABLE CONCURRENTLY links;
```

---

## 8. Security Hardening

### 8.1 RLS Verification

```sql
-- Verify RLS is enabled
SELECT
  tablename,
  rowsecurity
FROM pg_tables
WHERE schemaname = 'public';

-- Test RLS policies
-- 1. Tạo user mới
-- 2. Login với user đó
-- 3. Thử truy cập data của tenant khác
-- 4. Phải bị DENY
```

### 8.2 API Security Checklist

- [ ] Rate limiting enabled
- [ ] CORS configured đúng domains
- [ ] Input validation on all endpoints
- [ ] SQL injection prevention (parameterized queries)
- [ ] XSS prevention (output encoding)
- [ ] CSRF tokens for mutations
- [ ] Secure headers (CSP, X-Frame-Options, etc.)

### 8.3 Rotate Secrets

```bash
# Tạo script rotation
#!/bin/bash
# rotation.sh

# HiveMQ credentials
NEW_MQTT_PASS=$(openssl rand -base64 32)
# Update in HiveMQ Console
# Update in Vercel
vercel env add MQTT_PASSWORD

# Supabase keys
# Generate new keys in Supabase Settings → API
# Update Vercel env vars
```

---

## 9. Monitoring Setup

### 9.1 Sentry Integration

```bash
cd neutron-web
npm install @sentry/nextjs
npx sentry-wizard -i nextjs
```

### 9.2 Health Check Endpoint

```typescript
// app/api/health/route.ts
export async function GET() {
  const checks = {
    supabase: await checkSupabase(),
    mqtt: await checkMQTT(),
    database: await checkDatabase(),
    uptime: process.uptime(),
  };
  
  const healthy = Object.values(checks).every(v => v === true);
  
  return NextResponse.json({
    status: healthy ? 'healthy' : 'unhealthy',
    checks,
    timestamp: new Date().toISOString(),
  }, { status: healthy ? 200 : 503 });
}
```

### 9.3 Dashboard Alerts

Configure alerts trong Supabase Dashboard:

| Alert | Condition | Action |
|-------|-----------|--------|
| Database size | > 80% | Email |
| Connection count | > 80% | Email |
| Slow queries | > 1s for 5 min | Email |
| Error rate | > 1% | Slack |

---

## 10. Deployment Verification

### 10.1 Smoke Tests

```bash
# 1. Test health endpoint
curl https://your-app.vercel.app/api/health

# 2. Test device pairing flow
# - Request OTP
curl -X POST https://your-app.vercel.app/api/device/request-otp \
  -H "Content-Type: application/json" \
  -d '{"device_id": "GETAI-TEST1"}'

# 3. Test MQTT connection
mosquitto_pub -h xxxx.s1.eu.hivemq.cloud -p 8883 \
  -u username -P password \
  -t test \
  -m "hello"

# 4. Test Supabase connection
supabase
```

### 10.2 Performance Benchmarks

```bash
# API latency
for i in {1..100}; do
  time curl -s -o /dev/null -w "%{time_total}s\n" \
    https://your-app.vercel.app/api/health
done | awk '{sum+=$1; sumsq+=$1*$1} END {print "avg:", sum/NR, "p95:", sumsq/NR}'

# Expected:
# - p50: < 100ms
# - p95: < 500ms
# - p99: < 1000ms
```

### 10.3 Checklist Final

- [ ] All environment variables set
- [ ] Database migrations applied
- [ ] RLS policies verified
- [ ] Edge functions deployed
- [ ] MQTT credentials working
- [ ] Rate limiting tested
- [ ] Health endpoint returning 200
- [ ] Sentry receiving events
- [ ] Backup configured
- [ ] DNS configured (nếu có custom domain)

---

## 11. Rollback Procedures

### 11.1 Database Rollback

```bash
# List migrations
supabase migration list

# Rollback last migration
supabase db reset --db-url [backup-url]

# Hoặc restore from point-in-time
# Supabase Dashboard → Backups → Point in Time → Restore
```

### 11.2 Application Rollback

```bash
# List deployments
vercel ls

# Rollback to previous deployment
vercel rollback [deployment-url]

# Hoặc rollback qua dashboard
# Vercel → Deployments → [...] → Rollback
```

### 11.3 Edge Functions Rollback

```bash
# List function versions
supabase functions list

# Redeploy previous version
supabase functions deploy detect-patterns --no-verify-jwt
```

---

## 12. Troubleshooting

### 12.1 Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| API returns 500 | Supabase connection failed | Check DATABASE_URL |
| MQTT publish fails | Credentials wrong / TLS rejected | Verify MQTT_USERNAME/PASSWORD; check rejectUnauthorized setting |
| RLS blocks all queries | Policy too strict | Check `get_user_tenant_id()` function |
| Edge function timeout | Claude API slow | Increase timeout or use streaming |
| Rate limit triggered | Too many requests | Wait or increase limit |
| Database reset fails | Migration order wrong | Use `FULL_RESET.sql` — all-in-one |
| TypeScript errors | Missing packages | Pure TS implementations (no external deps needed) |

### 12.2 Debug Commands

```bash
# Check Supabase logs
supabase db logs --tail

# Check Edge function logs
supabase functions logs detect-patterns

# Test database connection
psql $DATABASE_URL -c "SELECT 1;"

# Check RLS
psql $DATABASE_URL -c "SELECT * FROM pg_policies WHERE tablename = 'nodes';"
```

---

## 13. File Reference

### Files to Create/Modify

```
espclaw/
├── neutron-web/
│   ├── lib/
│   │   ├── mqtt-pool.ts          ← NEW: Singleton MQTT connection pool
│   │   ├── mqtt.ts               ← UPDATED: Re-exports mqtt-pool
│   │   ├── validation.ts          ← NEW: Pure-TS validation (no zod dep)
│   │   ├── rate-limit.ts         ← NEW: Upstash RL + in-memory fallback
│   │   ├── logger.ts             ← NEW: Structured logging (no pino dep)
│   │   └── auto-link.ts          ← UPDATED: Fix pattern_id reference
│   ├── app/
│   │   └── api/
│   │       ├── device/
│   │       │   ├── request-otp/route.ts ← UPDATED: +Zod +rate limit
│   │       │   └── verify-otp/route.ts  ← UPDATED: +Zod +rate limit
│   │       ├── neuron/
│   │       │   ├── sync/route.ts  ← NEW: Delta sync endpoint
│   │       │   └── pulse/route.ts ← NEW: Pulse endpoint
│   │       └── health/route.ts   ← NEW: Health check endpoint
│   ├── types/
│   │   └── stubs.d.ts            ← NEW: Type stubs for missing packages
│   └── .env.example              ← NEW: Env template
│
├── supabase/
│   ├── migrations/
│   │   ├── FULL_RESET.sql        ← NEW: All-in-one migration (run this)
│   │   └── 002_security_tenant_isolation.sql ← UPDATED: RLS policies
│   └── functions/
│       └── auto-link/
│           └── index.ts           ← FIXED: pattern_id not auto_link_id
│
├── scripts/
│   ├── db-reset.js               ← NEW: Node.js DB reset script
│   └── smoke-tests.sh            ← NEW: API smoke tests
│
└── .gitignore                   ← UPDATED: +.env.local exclusion
```
│   │   ├── rate-limit.ts         ← NEW: Rate limiting middleware
│   │   └── realtime.ts           ← NEW: Enhanced realtime
│   ├── app/
│   │   ├── api/
│   │   │   ├── device/
│   │   │   │   ├── request-otp/
│   │   │   │   │   └── route.ts ← MODIFIED: Add rate limiting
│   │   │   │   └── verify-otp/
│   │   │   │       └── route.ts ← MODIFIED: Add validation
│   │   │   ├── neuron/
│   │   │   │   ├── sync/
│   │   │   │   │   └── route.ts ← NEW: Delta sync endpoint
│   │   │   │   ├── pulse/
│   │   │   │   │   └── route.ts ← NEW: Pulse endpoint
│   │   │   │   └── graph/
│   │   │   │       └── route.ts ← NEW: Full graph endpoint
│   │   │   └── lua/
│   │   │       ├── scripts/
│   │   │       │   └── route.ts ← NEW: Script CRUD
│   │   │       └── run/
│   │   │           └── route.ts ← NEW: Remote execution
│   │   └── health/
│   │       └── route.ts          ← NEW: Health check
│   └── components/
│       ├── ErrorBoundary.tsx      ← NEW: Error boundary
│       └── RealtimeStatus.tsx    ← NEW: Connection status
│
├── supabase/
│   ├── migrations/
│   │   ├── 001_security_rls.sql   ← NEW: Secure RLS
│   │   ├── 002_lua_scripts.sql    ← NEW: Script storage
│   │   ├── 003_script_executions.sql ← NEW: Analytics
│   │   └── 004_recommendations.sql ← NEW: AI recs
│   └── functions/
│       ├── detect-patterns/
│       │   └── index.ts          ← MODIFIED: Enhanced
│       ├── ai-insights/
│       │   └── index.ts          ← MODIFIED: Tool use
│       └── auto-link/
│           └── index.ts          ← MODIFIED: Dry-run mode
│
└── docs/
    ├── DEPLOYMENT.md             ← THIS FILE
    └── CHECKLIST.md              ← Implementation checklist
```

---

## 14. Version History

| Version | Date | Changes | Author |
|---------|------|---------|--------|
| 1.0 | 29/04/2026 | Initial deployment guide | ESPClaw Team |

---

> **Support:** Nếu có vấn đề, kiểm tra logs trong Supabase Dashboard hoặc Vercel Dashboard trước. Sau đó tham khảo Section 12 (Troubleshooting).
