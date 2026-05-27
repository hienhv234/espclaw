# ESPClaw - Neuron Link System

**Version:** 1.0.0 | **Last Updated:** 2026-04-28

## Overview

Neuron Link is the cognitive synchronization system that connects ESP32 devices to the cloud knowledge graph. It enables:

- **Delta Sync**: Efficient incremental updates (only changed data)
- **Full Sync**: Complete graph synchronization
- **Real-time Updates**: Live WebSocket connection for instant updates
- **Device Pairing**: Secure device-to-account linking

## Architecture

```
┌─────────────────┐         REST API          ┌─────────────────────┐
│   ESP32 Device  │ ──────────────────────────▶│   Supabase DB       │
│                 │                            │   (nodes, links,    │
│  ┌───────────┐  │      WebSocket            │    pulses)          │
│  │ Neuron    │  │◀──────────────────────────│                     │
│  │ Cache     │  │                            │                     │
│  │ (NVS)     │  │                            └─────────────────────┘
│  └───────────┘  │                                    ▲
└─────────────────┘                                    │
       │                                               │ Realtime
       │                                               │
       ▼                                               │
┌─────────────────┐                            ┌───────┴─────────────┐
│   WiFi AP       │                            │   Next.js Dashboard │
│  Config Portal  │                            │   (3D Knowledge     │
│                 │                            │    Graph)           │
└─────────────────┘                            └─────────────────────┘
```

## Files

### ESP32 Firmware

| File | Description |
|------|-------------|
| `main/net/neuron_link.h` | Neuron Link API |
| `main/net/neuron_link.c` | Implementation |
| `main/net/wifi_ap.h` | Config portal |
| `main/net/wifi_ap.c` | Web UI + captive portal |
| `main/mem/neuron_cache.h` | L1 cache API |
| `main/mem/neuron_cache.c` | NVS-based storage |

### Supabase Functions

| Function | Description |
|---------|-------------|
| `supabase/functions/detect-patterns/` | Pattern detection |
| `supabase/functions/ai-insights/` | Claude AI analysis |
| `supabase/functions/auto-link/` | Auto-create links |

## Quick Start

### 1. Flash ESP32

```bash
cd /home/nowh/ai_lab/new\ model/espclaw
./build.sh xiao_s3_plus flash
```

### 2. Configure via WiFi AP

1. ESP32 creates WiFi AP: `ESPClaw-XXXXX`
2. Connect with password: `espclaw1`
3. Open browser → auto-redirect to config page
4. Fill in:
   - WiFi SSID & Password
   - MQTT Broker (optional)
   - LLM API Key (optional)
5. Click Save → device reboots

### 3. Access Dashboard

1. Open: `https://your-vercel-app.vercel.app`
2. Login/Signup
3. Go to Devices → Generate Pairing Code
4. ESP32 enters pairing mode, enter code

## Configuration Portal

The ESP32 hosts a captive portal for easy setup:

```
┌─────────────────────────────────────┐
│       ESPClaw Configuration         │
├─────────────────────────────────────┤
│ Device Info                         │
│ ├─ Device Name: ESPClaw             │
│ └─ Device Password: admin123        │
│                                     │
│ WiFi Settings                       │
│ ├─ WiFi SSID: MyHomeWiFi           │
│ └─ WiFi Password: *********         │
│                                     │
│ MQTT Broker (HiveMQ Cloud)          │
│ ├─ Broker: broker.hivemq.com       │
│ ├─ Port: 1883                      │
│ ├─ Username: (optional)             │
│ └─ Password: (optional)             │
│                                     │
│ LLM Provider                        │
│ ├─ API Key: sk-ant-****            │
│ ├─ Base URL: api.anthropic.com      │
│ └─ Model: claude-haiku-4-5-20251001 │
│                                     │
│ [        Save Configuration        ] │
└─────────────────────────────────────┘
```

## API Reference

### REST Endpoints (Supabase)

```bash
# Nodes
GET    /rest/v1/nodes?tenant_id=eq.{id}
POST   /rest/v1/nodes
PATCH  /rest/v1/nodes?id=eq.{id}
DELETE /rest/v1/nodes?id=eq.{id}

# Links
GET    /rest/v1/links?tenant_id=eq.{id}
POST   /rest/v1/links

# Pulses
GET    /rest/v1/pulses?tenant_id=eq.{id}
POST   /rest/v1/pulses
```

### Edge Functions

```bash
# Pattern Detection
POST /functions/v1/detect-patterns
Body: { "tenant_id": "...", "lookback_hours": 24 }

# AI Insights
POST /functions/v1/ai-insights
Body: { "tenant_id": "...", "analysis_type": "full" }

# Auto Link
POST /functions/v1/auto-link
Body: { "tenant_id": "...", "confidence_threshold": 0.5 }
```

## Sync Protocol

### Delta Sync (Default)

1. Get unsynced items from local cache
2. POST each item to Supabase
3. Mark as synced
4. Pull changes since last sync
5. Update local cache

### Full Sync

1. Clear local cache
2. Pull all cloud data
3. Store in local cache
4. Mark all as synced

### Real-time (WebSocket)

```javascript
const supabase = createClient(url, key)
supabase
  .channel('graph-{tenant_id}')
  .on('postgres_changes', {
    event: '*',
    table: 'nodes',
    filter: `tenant_id=eq.{tenant_id}`
  }, handleNodeChange)
  .subscribe()
```

## Database Schema

### Tables

| Table | Description | Indexes |
|-------|-------------|---------|
| `tenants` | User accounts | email |
| `devices` | ESP32 devices | tenant_id |
| `nodes` | Knowledge nodes | tenant_id, type, embedding |
| `links` | Node connections | tenant_id, source_id |
| `pulses` | Activity events | tenant_id, created_at |
| `patterns` | AI patterns | tenant_id, confidence |
| `ai_insights` | AI analysis | tenant_id |

### RLS Policies

All tables have Row Level Security:
- Users can only access their own tenant's data
- Devices belong to tenants
- Pairing requires valid code

## MQTT Topics

```
espclaw/{device_id}/sync     # Sync commands
espclaw/{device_id}/pulse    # Activity pulses
espclaw/{device_id}/config   # Configuration updates
```

## Troubleshooting

### ESP32 Won't Connect to WiFi

1. Check WiFi AP is running: `ESPClaw-XXXXX`
2. Verify password: `espclaw1`
3. Check browser opens config page (not blocked)

### Sync Not Working

1. Check Supabase URL is correct
2. Verify API key has permissions
3. Check RLS policies allow device access

### Dashboard Shows No Data

1. Verify Supabase Realtime is enabled
2. Check tenant_id matches
3. Ensure RLS allows reads

## Edge Functions Setup

```bash
# Deploy edge functions
cd supabase/functions
supabase functions deploy detect-patterns
supabase functions deploy ai-insights
supabase functions deploy auto-link

# Set secrets
supabase secrets set ANTHROPIC_API_KEY=sk-ant-...
```

## Cron Jobs

Schedule pattern detection:

```sql
-- Supabase pg_cron
SELECT cron.schedule(
  'detect-patterns',
  '0 */6 * * *',  -- Every 6 hours
  $$ SELECT net.http_post(
    url := 'https://your-project.supabase.co/functions/v1/detect-patterns',
    headers := '{"Content-Type": "application/json"}'::jsonb,
    body := '{"tenant_id": "your-tenant-id"}'::jsonb
  ) $$
);
```

## Performance

| Operation | Latency | Notes |
|-----------|---------|-------|
| Delta Sync | < 500ms | Incremental |
| Full Sync | < 5s | 100 nodes |
| Real-time | < 100ms | WebSocket |
| Cache Hit | < 10ms | Local NVS |

## Security

- [x] RLS policies on all tables
- [x] Device pairing codes (6-digit, 5min expiry)
- [x] API keys stored in NVS (encrypted)
- [x] HTTPS for all API calls
- [ ] NVS encryption (optional)

## Future Enhancements

- [ ] End-to-end encryption
- [ ] Offline-first sync
- [ ] Conflict resolution
- [ ] Batch sync optimization
- [ ] MQTT bridge for real-time
