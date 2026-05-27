# ESPClaw — Backend Implementation Checklist

> **Phiên bản:** 1.0 | **Ngày:** 29/04/2026
> **Trạng thái:** Đang triển khai | **Độ ưu tiên:** 🔴 Cao | 🟡 Trung | 🟢 Thấp

---

## Mục lục

1. [Phase 1: Security & Production Hardening](#phase-1--security--production-hardening)
2. [Phase 2: NeuronLink Enhancement](#phase-2--neuronlink-enhancement)
3. [Phase 3: Lua Ecosystem](#phase-3--lua-ecosystem)
4. [Phase 4: AI Pipeline](#phase-4--ai-pipeline)
5. [Phase 5: Monitoring & Observability](#phase-5--monitoring--observability)
6. [Quick Reference](#quick-reference)

---

## PHASE 1: Security & Production Hardening

### 1.1 MQTT Credential Management 🔴 CAO

- [ ] **1.1.1** Di chuyển MQTT credentials từ source code sang environment variables
  - File: `neutron-web/lib/mqtt.ts`
  - File: `neutron-web/lib/auth.ts`
  - Thêm vào `.env.local`:
    ```bash
    NEXT_PUBLIC_MQTT_BROKER_URL=mqtts://broker:8883
    MQTT_USERNAME=your_username
    MQTT_PASSWORD=your_password
    ```
  - Xóa hardcoded credentials khỏi tất cả file

- [ ] **1.1.2** Xóa credentials cũ khỏi Git history
  ```bash
  git filter-branch --force --index-filter \
    'git rm --cached --ignore-unmatch neutron-web/lib/mqtt.ts' \
    --prune-empty --tag-name-filter cat -- --all
  ```

- [ ] **1.1.3** Implement persistent MQTT connection pool
  - Thay vì connect-per-request, dùng singleton connection
  - Xem: `neutron-web/lib/mqtt-pool.ts` (cần tạo)
  - Test reconnect logic với exponential backoff

- [ ] **1.1.4** Rotate HiveMQ credentials
  - Đăng nhập HiveMQ Cloud Console
  - Tạo credentials mới cho backend
  - Cập nhật Vercel environment variables

### 1.2 RLS & Tenant Isolation 🔴 CAO

- [ ] **1.2.1** Nâng cấp RLS policies từ permissive sang secure
  - File: `supabase/migrations/COMPLETE_MIGRATION.sql`
  - Thay thế:
    ```sql
    -- Cũ (permissive):
    CREATE POLICY "Users can view nodes" ON nodes
      FOR SELECT USING (auth.role() = 'authenticated');
    
    -- Mới (secure):
    CREATE POLICY "Users can view own tenant nodes" ON nodes
      FOR SELECT USING (
        tenant_id = (
          SELECT raw_user_meta_data->>'tenant_id'
          FROM auth.users WHERE id = auth.uid()
        )
      );
    ```

- [ ] **1.2.2** Thêm index cho tenant_id trên tất cả tables
  ```sql
  -- Kiểm tra index hiện có:
  SELECT tablename, indexname FROM pg_indexes 
  WHERE schemaname = 'public' AND indexname LIKE '%tenant%';
  
  -- Tạo index nếu thiếu:
  CREATE INDEX IF NOT EXISTS idx_pulses_tenant ON pulses(tenant_id);
  CREATE INDEX IF NOT EXISTS idx_patterns_tenant ON patterns(tenant_id);
  CREATE INDEX IF NOT EXISTS idx_ai_insights_tenant ON ai_insights(tenant_id);
  ```

- [ ] **1.2.3** Thêm function để set tenant context
  ```sql
  CREATE OR REPLACE FUNCTION set_tenant_context(tenant_uuid UUID)
  RETURNS VOID AS $$
  BEGIN
    PERFORM set_config('app.tenant_id', tenant_uuid::TEXT, true);
  END;
  $$ LANGUAGE plpgsql SECURITY DEFINER;
  ```

### 1.3 API Rate Limiting 🔴 CAO

- [ ] **1.3.1** Đăng ký Upstash Redis
  - Truy cập https://upstash.com
  - Tạo database mới
  - Lấy REST API URL và token

- [ ] **1.3.2** Cài đặt rate limit middleware
  ```bash
  cd neutron-web
  npm install @upstash/ratelimit @upstash/redis
  ```

- [ ] **1.3.3** Tạo rate limit configuration
  - File: `neutron-web/lib/rate-limit.ts`
  - Endpoint `/api/device/*`: 10 req/min/IP
  - Endpoint `/api/neuron/*`: 60 req/min/user
  - Endpoint `/api/lua/*`: 20 req/min/user

- [ ] **1.3.4** Áp dụng vào tất cả API routes
  - `app/api/device/request-otp/route.ts`
  - `app/api/device/verify-otp/route.ts`
  - `app/api/neuron/sync/route.ts`
  - `app/api/neuron/pulse/route.ts`

### 1.4 Input Validation 🔴 CAO

- [ ] **1.4.1** Thêm Zod schema validation cho tất cả API routes
  ```bash
  cd neutron-web
  npm install zod
  ```

- [ ] **1.4.2** Validate device_id format (GETAI-XXXXX)
  ```typescript
  const deviceIdSchema = z.string()
    .regex(/^GETAI-[A-Z0-9]{5}$/, 'Invalid device ID format')
    .transform(v => v.toUpperCase());
  ```

- [ ] **1.4.3** Validate OTP format (6 digits)
  ```typescript
  const otpSchema = z.string()
    .regex(/^\d{6}$/, 'OTP must be 6 digits');
  ```

- [ ] **1.4.4** Validate JSON content từ Supabase
  - Parse JSON fields với fallback graceful
  - Log malformed data nhưng không crash

### 1.5 Error Tracking 🔴 TRUNG

- [ ] **1.5.1** Đăng ký Sentry
  - Truy cập https://sentry.io
  - Tạo project mới cho ESPClaw
  - Lấy DSN

- [ ] **1.5.2** Cài đặt Sentry SDK
  ```bash
  cd neutron-web
  npm install @sentry/nextjs
  npx sentry-wizard -i nextjs
  ```

- [ ] **1.5.3** Thêm error boundary component
  - File: `neutron-web/components/ErrorBoundary.tsx`
  - Wrap dashboard page

- [ ] **1.5.4** Set up alert rules trong Sentry
  - Alert khi error rate > 1%
  - Alert khi p99 latency > 2s

---

## PHASE 2: NeuronLink Enhancement

### 2.1 Script Storage & Sync 🔴 CAO

- [ ] **2.1.1** Tạo Supabase Storage bucket cho Lua scripts
  ```sql
  INSERT INTO storage.buckets (id, name, public)
  VALUES ('lua-scripts', 'lua-scripts', false);
  ```

- [ ] **2.1.2** Thêm RLS policies cho storage
  ```sql
  CREATE POLICY "Users can upload own scripts" ON storage.objects
    FOR INSERT WITH CHECK (
      bucket_id = 'lua-scripts' AND
      auth.uid()::TEXT = (storage.foldername(name))[1]
    );
  ```

- [ ] **2.1.3** Tạo bảng lua_scripts metadata
  ```sql
  CREATE TABLE lua_scripts (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id UUID REFERENCES tenants(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    path TEXT NOT NULL,
    content_hash TEXT NOT NULL,
    size_bytes INTEGER NOT NULL,
    category TEXT DEFAULT 'general',
    tags TEXT[],
    is_public BOOLEAN DEFAULT FALSE,
    is_builtin BOOLEAN DEFAULT FALSE,
    downloads INTEGER DEFAULT 0,
    version INTEGER DEFAULT 1,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
  );
  ```

- [ ] **2.1.4** Implement upload API
  - File: `neutron-web/app/api/lua/upload/route.ts`
  - Max file size: 16KB
  - Validate .lua extension
  - Calculate SHA256 checksum

### 2.2 Pattern Detection Enhancement 🟡 TRUNG

- [ ] **2.2.1** Nâng cấp detect-patterns function
  ```typescript
  // Thêm temporal analysis
  interface TemporalPattern {
    sourceType: string;
    targetType: string;
    avgIntervalSeconds: number;
    varianceSeconds: number;
    periodicity: 'hourly' | 'daily' | 'weekly' | 'random';
  }
  ```

- [ ] **2.2.2** Thêm autocorrelation detection
  ```typescript
  function detectPeriodicity(intervals: number[]): string {
    const avg = intervals.reduce((a, b) => a + b, 0) / intervals.length;
    const hour = 3600, day = 86400, week = 604800;
    
    if (Math.abs(avg - hour) / hour < 0.1) return 'hourly';
    if (Math.abs(avg - day) / day < 0.1) return 'daily';
    if (Math.abs(avg - week) / week < 0.1) return 'weekly';
    return 'random';
  }
  ```

- [ ] **2.2.3** Set up pg_cron job
  ```sql
  SELECT cron.schedule(
    'detect-patterns',
    '0 */6 * * *',  -- Every 6 hours
    $$ SELECT net.http_post(
      url := 'https://project.supabase.co/functions/v1/detect-patterns',
      headers := '{"Authorization": "Bearer SERVICE_ROLE_KEY"}'::jsonb,
      body := '{"tenant_id": "all-active"}'::jsonb
    ) $$
  );
  ```

### 2.3 Realtime Enhancement 🟡 TRUNG

- [ ] **2.3.1** Tạo enhanced realtime channel
  - File: `neutron-web/lib/realtime.ts`
  - Optimistic updates
  - Automatic reconnection

- [ ] **2.3.2** Thêm subscription status indicator
  - Component: `components/RealtimeStatus.tsx`
  - States: connected, reconnecting, disconnected

- [ ] **2.3.3** Implement offline queue
  - Cache mutations khi offline
  - Sync khi reconnected

### 2.4 Delta Sync Optimization 🟡 TRUNG

- [ ] **2.4.1** Configurable sync intervals
  ```typescript
  // neutron-web/lib/sync-config.ts
  export const SYNC_CONFIG = {
    MIN_INTERVAL_MS: 30_000,    // 30s minimum
    MAX_INTERVAL_MS: 300_000,    // 5min maximum
    ON_DEMAND_THRESHOLD: 100,    // Force sync after 100 local changes
  };
  ```

- [ ] **2.4.2** Implement conflict resolution
  ```typescript
  type ConflictResolution = 'cloud-wins' | 'device-wins' | 'newest-wins' | 'manual';
  
  function resolveConflict(local: Node, remote: Node): Node {
    // Cloud wins by default for knowledge graph
    return remote;
  }
  ```

- [ ] **2.4.3** Add batch upsert for performance
  ```typescript
  await supabase.from('nodes').upsert(changes, {
    onConflict: 'id',
    ignoreDuplicates: false,
    batchSize: 50,
  });
  ```

---

## PHASE 3: Lua Ecosystem

### 3.1 Script Marketplace 🟡 TRUNG

- [ ] **3.1.1** Tạo marketplace UI page
  - File: `neutron-web/app/marketplace/page.tsx`
  - Categories: gpio, display, audio, automation, sensor
  - Search và filter functionality

- [ ] **3.1.2** Implement script rating system
  ```sql
  CREATE TABLE lua_script_ratings (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    script_id UUID REFERENCES lua_scripts(id) ON DELETE CASCADE,
    user_id UUID REFERENCES auth.users(id) ON DELETE CASCADE,
    rating INTEGER CHECK (rating >= 1 AND rating <= 5),
    created_at TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(script_id, user_id)
  );
  ```

- [ ] **3.1.3** Add script templates
  - Template cho LED animations
  - Template cho sensor reading
  - Template cho automation rules

### 3.2 Script Execution Analytics 🟡 TRUNG

- [ ] **3.2.1** Tạo bảng script_executions
  ```sql
  CREATE TABLE script_executions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id UUID REFERENCES tenants(id) ON DELETE CASCADE,
    device_id UUID REFERENCES devices(id) ON DELETE SET NULL,
    script_id UUID REFERENCES lua_scripts(id) ON DELETE SET NULL,
    script_path TEXT NOT NULL,
    mode TEXT NOT NULL CHECK (mode IN ('sync', 'async')),
    job_id TEXT,
    duration_ms INTEGER,
    status TEXT NOT NULL CHECK (status IN ('running', 'done', 'failed', 'timeout', 'stopped')),
    error_message TEXT,
    output_truncated BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMPTZ DEFAULT NOW()
  );
  ```

- [ ] **3.2.2** Create execution dashboard
  - File: `neutron-web/app/dashboard/executions/page.tsx`
  - Charts: success rate, avg duration, top scripts

- [ ] **3.2.3** Implement execution logging
  ```typescript
  async function logExecution(execution: ExecutionLog) {
    await supabase.from('script_executions').insert(execution);
  }
  ```

### 3.3 Script Versioning 🟢 THẤP

- [ ] **3.3.1** Add version tracking
  ```sql
  ALTER TABLE lua_scripts ADD COLUMN IF NOT EXISTS version INTEGER DEFAULT 1;
  ALTER TABLE lua_scripts ADD COLUMN IF NOT EXISTS parent_version INTEGER;
  ```

- [ ] **3.3.2** Implement rollback API
  ```typescript
  async function rollbackScript(scriptId: string, targetVersion: number) {
    const versions = await supabase
      .from('lua_script_versions')
      .select('*')
      .eq('script_id', scriptId)
      .eq('version', targetVersion);
    
    if (versions.data) {
      await supabase.from('lua_scripts').update({
        content: versions.data[0].content,
        version: targetVersion,
      }).eq('id', scriptId);
    }
  }
  ```

### 3.4 Built-in Scripts Enhancement 🟢 THẤP

- [ ] **3.4.1** Tạo thêm built-in scripts
  - `builtin/sensor_logger.lua` - Continuous sensor logging
  - `builtin/wifi_status.lua` - WiFi status display
  - `builtin/mqtt_test.lua` - MQTT connectivity test

- [ ] **3.4.2** Document all built-in modules
  - Tạo `docs/LUA_MODULES.md`
  - Include example code cho mỗi module

---

## PHASE 4: AI Pipeline

### 4.1 Enhanced AI Insights 🔴 CAO

- [ ] **4.1.1** Nâng cấp ai-insights function với tool use
  ```typescript
  // supabase/functions/ai-insights-v2/index.ts
  const SYSTEM_PROMPT = `You are a knowledge graph analyst.
  You have access to tools: search_nodes, get_node_connections,
  create_link, get_pulse_history. Analyze and improve the graph.`;
  ```

- [ ] **4.1.2** Thêm analysis types
  - `summary`: Quick 2-3 sentence overview
  - `suggestions`: 3-5 actionable recommendations
  - `full`: Comprehensive analysis

- [ ] **4.1.3** Implement Claude Sonnet 4 integration
  ```typescript
  const response = await fetch('https://api.anthropic.com/v1/messages', {
    headers: {
      'x-api-key': Deno.env.get('ANTHROPIC_API_KEY'),
      'anthropic-version': '2023-06-01',
    },
    body: {
      model: 'claude-sonnet-4-5-20251101',
      max_tokens: 2048,
      tools: [...],
      messages: [{ role: 'user', content: prompt }],
    },
  });
  ```

### 4.2 Automated Node Generation 🟡 TRUNG

- [ ] **4.2.1** Tạo generate-nodes function
  ```typescript
  // supabase/functions/generate-nodes/index.ts
  interface NodeGenerationRequest {
    tenant_id: string;
    source: 'sensor' | 'telegram' | 'cron' | 'api';
    data: {
      name: string;
      type: NodeType;
      subtype?: string;
      content?: Record<string, any>;
      position?: { x: number; y: number; z: number };
    };
  }
  ```

- [ ] **4.2.2** Implement auto-node from sensor data
  - Khi user hỏi về "cảm biến nhiệt độ" → tạo node `sensor_read`
  - Khi user đặt lịch "8h sáng" → tạo node `schedule_task`
  - Khi ESP gửi GPIO pulse → tạo node `gpio_event`

- [ ] **4.2.3** Add embedding generation
  ```typescript
  async function generateEmbedding(text: string): Promise<number[]> {
    const response = await fetch('https://api.openai.com/v1/embeddings', {
      headers: { 'Authorization': `Bearer ${OPENAI_API_KEY}` },
      body: { input: text, model: 'text-embedding-3-small' },
    });
    const data = await response.json();
    return data.data[0].embedding;
  }
  ```

### 4.3 Vector Similarity Pipeline 🟡 TRUNG

- [ ] **4.3.1** Tạo embed-and-link function
  ```typescript
  // supabase/functions/embed-and-link/index.ts
  async function autoLinkFromEmbedding(tenantId: string, nodeId: string) {
    // 1. Get node embedding
    // 2. Find similar nodes using vector search
    // 3. Create semantic links for similarity > 0.9
  }
  ```

- [ ] **4.3.2** Thêm HNSW index cho better recall
  ```sql
  -- Thay IVFFlat bằng HNSW
  CREATE INDEX idx_nodes_embedding_hnsw ON nodes
    USING hnsw (embedding vector_cosine_ops)
    WITH (m = 16, ef_construction = 64);
  ```

- [ ] **4.3.3** Implement similarity threshold config
  ```typescript
  const SIMILARITY_CONFIG = {
    AUTO_LINK_THRESHOLD: 0.9,
    SUGGESTION_THRESHOLD: 0.7,
    SEARCH_LIMIT: 10,
  };
  ```

### 4.4 Recommendation Engine 🟢 THẤP

- [ ] **4.4.1** Tạo bảng recommendations
  ```sql
  CREATE TABLE recommendations (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id UUID REFERENCES tenants(id) ON DELETE CASCADE,
    type TEXT NOT NULL,  -- 'node_suggestion' | 'link_suggestion' | 'automation'
    priority INTEGER DEFAULT 0,
    data JSONB NOT NULL,
    status TEXT DEFAULT 'pending',  -- 'pending' | 'approved' | 'rejected'
    created_at TIMESTAMPTZ DEFAULT NOW(),
    resolved_at TIMESTAMPTZ
  );
  ```

- [ ] **4.4.2** Implement recommendation UI
  - File: `neutron-web/app/dashboard/recommendations/page.tsx`
  - Approve/reject buttons
  - Bulk actions

---

## PHASE 5: Monitoring & Observability

### 5.1 Database Monitoring 🟡 TRUNG

- [ ] **5.1.1** Set up Supabase dashboard alerts
  - Database size > 80% capacity
  - Connection count > 80% limit
  - Slow queries > 1s

- [ ] **5.1.2** Tạo monitoring queries
  ```sql
  -- Device health
  SELECT d.device_name, d.is_online, 
         EXTRACT(EPOCH FROM (NOW() - d.last_seen_at))/3600 as hours_ago
  FROM devices d WHERE d.tenant_id = $1
  ORDER BY d.last_seen_at DESC;
  
  -- Top activated nodes
  SELECT n.name, n.type, COUNT(p.id) as pulse_count_7d
  FROM nodes n LEFT JOIN pulses p ON p.target_node_id = n.id
    AND p.created_at > NOW() - INTERVAL '7 days'
  WHERE n.tenant_id = $1 AND n.is_active = TRUE
  GROUP BY n.id ORDER BY pulse_count_7d DESC LIMIT 20;
  
  -- Orphan nodes
  SELECT n.id, n.name, n.type
  FROM nodes n LEFT JOIN links l ON l.source_node_id = n.id OR l.target_node_id = n.id
  WHERE n.tenant_id = $1 AND n.is_active = TRUE
  GROUP BY n.id HAVING COUNT(l.id) = 0;
  ```

- [ ] **5.1.3** Set up pg_cron monitoring
  ```sql
  SELECT cron.schedule(
    'health-check',
    '*/5 * * * *',
    $$ INSERT INTO health_checks (timestamp, status) VALUES (NOW(), 'ok') $$
  );
  ```

### 5.2 Application Monitoring 🟡 TRUNG

- [ ] **5.2.1** Add custom metrics
  ```typescript
  // Track key metrics
  metrics.increment('api.device.request_otp', { status: 'success' });
  metrics.histogram('api.neuron.sync.duration_ms', duration);
  metrics.gauge('active.devices', deviceCount);
  ```

- [ ] **5.2.2** Create Grafana dashboard
  - Import JSON dashboard template
  - Configure panels: API latency, error rate, active devices, sync status

- [ ] **5.2.3** Set up uptime monitoring
  - Use UptimeRobot hoặc Better Uptime
  - Monitor: Dashboard URL, API endpoints, MQTT broker

### 5.3 Lua Runtime Monitoring 🟡 TRUNG

- [ ] **5.3.1** Track execution metrics
  ```sql
  -- Script execution stats
  SELECT script_path, mode, status, COUNT(*) as count,
         AVG(duration_ms) as avg_duration_ms
  FROM script_executions
  WHERE created_at > NOW() - INTERVAL '7 days'
  GROUP BY script_path, mode, status;
  
  -- Failure analysis
  SELECT script_path, error_message, COUNT(*) as count
  FROM script_executions
  WHERE status = 'failed' AND created_at > NOW() - INTERVAL '24 hours'
  GROUP BY script_path, error_message
  ORDER BY count DESC;
  ```

- [ ] **5.3.2** Create execution alerts
  - Alert khi failure rate > 5%
  - Alert khi avg duration > 10s
  - Alert khi concurrent jobs > 3

---

## QUICK REFERENCE

### Critical Paths

```
┌─────────────────────────────────────────────────────────────┐
│ CRITICAL PATH: Device Pairing Flow                         │
│                                                             │
│ 1. User nhập device_id (GETAI-XXXXX)                      │
│    → 1.1 Validate format (regex) ✅                      │
│    → 1.2 Rate limit check ✅                             │
│                                                             │
│ 2. Backend request OTP                                     │
│    → 2.1 Find/create device record ✅                      │
│    → 2.2 Generate 6-digit OTP ✅                           │
│    → 2.3 Store with 5min expiry ✅                        │
│    → 2.4 Publish via MQTT ✅                              │
│                                                             │
│ 3. ESP nhận OTP → hiển thị trên OLED                     │
│    → 3.1 Subscribe espclaw/{id}/otp ✅                    │
│    → 3.2 Parse JSON payload ✅                             │
│    → 3.3 Display on screen ✅                             │
│                                                             │
│ 4. User nhập OTP → verify                                 │
│    → 4.1 Validate OTP (6 digits) ✅                       │
│    → 4.2 Check expiry ✅                                  │
│    → 4.3 Mark device as paired ✅                         │
│    → 4.4 Publish login_success via MQTT ✅                │
│    → 4.5 Return device info ✅                            │
│                                                             │
│ 5. ESP nhận login_success                                │
│    → 5.1 Save tenant_id to NVS ✅                        │
│    → 5.2 Start NeuronLink sync ✅                         │
│    → 5.3 Switch to ready screen ✅                        │
│                                                             │
│ TOTAL: 20 sub-steps                                        │
│ COMPLETED: [ ]                                            │
└─────────────────────────────────────────────────────────────┘
```

### Environment Variables Checklist

```bash
# Supabase
NEXT_PUBLIC_SUPABASE_URL=https://xxx.supabase.co
NEXT_PUBLIC_SUPABASE_ANON_KEY=eyJ...
SUPABASE_SERVICE_ROLE_KEY=eyJ...
DATABASE_URL=postgresql://...

# MQTT
NEXT_PUBLIC_MQTT_BROKER_URL=mqtts://xxx:8883
MQTT_USERNAME=xxx
MQTT_PASSWORD=xxx

# LLM (Edge Functions)
ANTHROPIC_API_KEY=sk-ant-...
OPENAI_API_KEY=sk-...

# Rate Limiting
UPSTASH_REDIS_REST_URL=https://xxx.upstash.io
UPSTASH_REDIS_REST_TOKEN=xxx

# Monitoring
SENTRY_DSN=https://xxx@sentry.io/xxx

# App
NEXT_PUBLIC_APP_URL=https://yourapp.vercel.app
```

### Database Migration Order

```
1. security_rls_policies.sql        ← RLS enhancement
2. script_metadata.sql             ← lua_scripts table
3. script_executions.sql          ← execution tracking
4. recommendations.sql             ← AI recommendations
5. materialized_views.sql         ← graph stats MV
6. health_checks.sql              ← monitoring
7. trigger_functions.sql          ← Updated triggers
```

### Test Scenarios

| ID | Scenario | Steps | Expected Result |
|----|----------|-------|----------------|
| T1 | Device pairing thành công | 1→2→3→4→5 | Device online, dashboard hiển thị |
| T2 | OTP hết hạn | Request OTP, đợi 6 phút, verify | Error: "OTP expired" |
| T3 | Rate limit exceeded | Gửi 15 request OTP trong 1 phút | 429 Too Many Requests |
| T4 | Realtime pulse | Click node trên graph | Pulse animation 60s |
| T5 | Lua script execution | Run builtin/led_strip.lua | LED rainbow animation |
| T6 | Async job stop | Start long script, call stop | Job stopped, status=stopped |
| T7 | Vector similarity | Create similar nodes | Auto-link suggestion appears |

---

## Progress Tracking

### Phase Completion

| Phase | Tasks | Completed | % | Owner | Due Date |
|-------|-------|-----------|---|-------|----------|
| Phase 1 | 24 | 0 | 0% | - | - |
| Phase 2 | 12 | 0 | 0% | - | - |
| Phase 3 | 11 | 0 | 0% | - | - |
| Phase 4 | 9 | 0 | 0% | - | - |
| Phase 5 | 7 | 0 | 0% | - | - |
| Phase 6 | 6 | 0 | 0% | - | - |

### Daily Standup Checklist

- [ ] Review previous day's deployments
- [ ] Check error rates (Sentry)
- [ ] Check Supabase metrics
- [ ] Run smoke tests
- [ ] Update task status
- [ ] Note blockers

---

> **Ghi chú:** Checklist này được cập nhật định kỳ. Mỗi task nên có PR riêng với tests và documentation.
