# NEUTRON-ESPClaw: 3D Knowledge Graph Architecture

## Executive Summary

This document defines the **Graph Schema** — the foundational data architecture for the Neutron-ESPClaw cognitive system. The schema is designed to support spatial intelligence, real-time edge-cloud synchronization, and self-evolving knowledge graphs.

---

## 1. Core Philosophy

### 1.1 Why Graph-First?

```
┌─────────────────────────────────────────────────────────────────┐
│                    TRADITIONAL APPROACH                          │
│  User → Command → If-Else Logic → Response                     │
│  Problem: Rigid, no context awareness, hard to extend            │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                    NEUTRON APPROACH                              │
│  User → Entity → Neural Pulse → Graph Traverse → Response       │
│  ✦ Context-aware: Each entity knows its relationships           │
│  ✦ Self-organizing: Graph evolves with user behavior            │
│  ✦ Spatial: 3D coordinates enable visual reasoning              │
│  ✦ Modular: Add new skill types without redesigning core        │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Design Principles

1. **Everything is a Node** — Users, Skills, Memories, Tags, Devices, Transactions
2. **Relationships are First-Class** — Links have types, weights, and timestamps
3. **Space is Meaningful** — (X, Y, Z) coordinates enable spatial reasoning
4. **Tenant Isolation is Absolute** — RLS policies enforce data sovereignty
5. **Sync is Bidirectional** — ESP32 SQLite ↔ Supabase PostgreSQL

---

## 2. Spatial Intelligence System

### 2.1 3D Coordinate Space

Each node exists in a 3D semantic space where **proximity = similarity**:

```
                    Z (Depth - Time/Recency)
                     ↑
                     │     [Older Memories]
                     │        ↓
                     │   +---------+
                     │   │ Finance │  ← Cluster: Financial domain
                     │   +---------+
                     │      ↑
                     │   [Transactions]
                     │
Y (Domain) ──────────┼────────────────────────────────────────→
                     │      ↑
                     │   [Current Context]
                     │        ↓
                     │   +---------+
                     │   │ Voice   │  ← Cluster: Input sources
                     │   +---------+
                     │
                     │     [Recent Inputs]
                     │        ↓
                     │
                     └──────────────────────────────────────────→ X (Importance)
                              [Low Importance]  [High Importance]
```

### 2.2 Coordinate Assignment Rules

```javascript
// Coordinate calculation based on node properties
function assignCoordinates(node, existingNodes) {
  let baseX = node.importance * 10;           // 0-10 importance → 0-100 X
  let baseY = domainMapping[node.domain];      // Domain → Y layer
  let baseZ = recencyScore(node.created_at);  // Newer = closer (lower Z)
  
  // Add noise to prevent overlap
  let jitter = randomVector() * 5;
  
  // Pull toward semantically similar nodes
  let pull = calculateSemanticPull(node, existingNodes) * 0.3;
  
  return {
    x: baseX + jitter.x + pull.x,
    y: baseY + jitter.y + pull.y,
    z: baseZ + jitter.z + pull.z
  };
}
```

### 2.3 Domain Layers (Y-Axis)

| Y Range | Domain | Description |
|---------|--------|-------------|
| 0-99 | SYSTEM | Core infrastructure nodes |
| 100-199 | VOICE | Input sources, voice commands |
| 200-299 | CONTEXT | Current session, active entities |
| 300-399 | FINANCE | Money, transactions, budgets |
| 400-499 | HEALTH | Fitness, medical, wellness |
| 500-599 | HOME | Smart home, IoT devices |
| 600-699 | SOCIAL | Contacts, communications |
| 700-799 | KNOWLEDGE | Facts, learnings, documents |
| 800-899 | CREATIVE | Art, music, projects |
| 900-999 | MEMORY | Long-term memories, archives |

---

## 3. Node Type Taxonomy

### 3.1 Primary Node Types

```sql
-- Enum: node_types
CREATE TYPE node_type AS ENUM (
  'user',           -- Root user profile
  'device',        -- ESP32 device instance
  'skill',         -- Micro-skill module
  'memory',        -- Stored experience/memory
  'tag',           -- Categorization tag
  'transaction',  -- Financial transaction
  'entity',        -- Recognized entity (person, place, thing)
  'event',         -- Timestamped event
  'goal',          -- User goal or objective
  'routine',       -- Repeated behavior pattern
  'insight',       -- AI-generated insight
  'pulse',         -- Real-time inference pulse
  'webhook',       -- External integration
  'session',       -- Conversation session
  'context'        -- Current context snapshot
);
```

### 3.2 Node Metadata Schema

```sql
CREATE TABLE nodes (
  -- Identity
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  -- Classification
  type            node_type NOT NULL,
  subtype         TEXT,                              -- e.g., 'finance_budget', 'health_exercise'
  
  -- Content
  name            TEXT NOT NULL,
  description     TEXT,
  content         JSONB,                            -- Flexible content storage
  embedding       VECTOR(1536),                     -- OpenAI text-embedding-3-small
  
  -- Spatial Position (3D Knowledge Graph)
  pos_x           FLOAT NOT NULL DEFAULT 50,
  pos_y           FLOAT NOT NULL DEFAULT 500,
  pos_z           FLOAT NOT NULL DEFAULT 50,
  
  -- Temporal
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  accessed_at     TIMESTAMPTZ,
  
  -- Lifecycle
  is_active       BOOLEAN NOT NULL DEFAULT TRUE,
  is_pinned       BOOLEAN NOT NULL DEFAULT FALSE,
  is_favorite     BOOLEAN NOT NULL DEFAULT FALSE,
  
  -- Provenance
  source_device   UUID REFERENCES devices(id),       -- Which ESP32 created this
  source_session  UUID REFERENCES sessions(id),     -- Which session spawned this
  
  -- Statistics
  activation_count INT NOT NULL DEFAULT 0,           -- How often this node was accessed
  link_count      INT NOT NULL DEFAULT 0,           -- Number of active links
  pulse_strength  FLOAT NOT NULL DEFAULT 0,         -- 0-1, used for visualization
  
  -- Constraints
  UNIQUE(tenant_id, type, name)
);

-- GIN index for content search
CREATE INDEX idx_nodes_content ON nodes USING GIN (content jsonb_path_ops);
CREATE INDEX idx_nodes_embedding ON nodes USING ivfflat (embedding vector_cosine_ops);
CREATE INDEX idx_nodes_type ON nodes (tenant_id, type);
CREATE INDEX idx_nodes_spatial ON nodes (tenant_id, pos_x, pos_y, pos_z);
```

### 3.3 Node Subtypes by Domain

```javascript
const NODE_SUBTYPES = {
  // Finance Domain (Y: 300-399)
  finance_account: { icon: '🏦', color: '#4CAF50' },
  finance_transaction: { icon: '💸', color: '#8BC34A' },
  finance_budget: { icon: '📊', color: '#009688' },
  finance_goal: { icon: '🎯', color: '#00796B' },
  finance_subscription: { icon: '🔄', color: '#00695C' },
  
  // Health Domain (Y: 400-499)
  health_exercise: { icon: '🏃', color: '#F44336' },
  health_meal: { icon: '🍽️', color: '#E91E63' },
  health_sleep: { icon: '😴', color: '#9C27B0' },
  health_metric: { icon: '📈', color: '#673AB7' },
  health_medication: { icon: '💊', color: '#3F51B5' },
  
  // Home Domain (Y: 500-599)
  home_device: { icon: '🔌', color: '#FF9800' },
  home_room: { icon: '🏠', color: '#FFC107' },
  home_scene: { icon: '🎬', color: '#FF5722' },
  home_sensor: { icon: '📡', color: '#795548' },
  
  // Skill Domain (X: High Importance)
  skill_active: { icon: '⚡', color: '#2196F3' },
  skill_learning: { icon: '📚', color: '#03A9F4' },
  skill_dormant: { icon: '💤', color: '#00BCD4' },
  
  // Memory Domain (Z: Time-based)
  memory_episodic: { icon: '📼', color: '#9E9E9E' },
  memory_semantic: { icon: '🧠', color: '#607D8B' },
  memory_working: { icon: '💭', color: '#78909C' }
};
```

---

## 4. Link (Edge) Type Taxonomy

### 4.1 Link Types

```sql
-- Enum: link_types
CREATE TYPE link_type AS ENUM (
  -- Structural Links
  'owns',              -- User owns a device/skill
  'part_of',           -- Entity is part of a larger entity
  'member_of',         -- Member of a group
  
  -- Semantic Links
  'related_to',         -- General semantic relationship
  'similar_to',        -- Similar to another node
  'caused_by',         -- This caused something
  'resulted_in',       -- This was the result
  
  -- Temporal Links
  'happened_before',   -- Temporal precedence
  'happened_after',    -- Temporal following
  'triggered',         -- This triggered something
  
  -- Behavioral Links
  'followed_by',       -- User typically does this after
  'preceded_by',      -- User typically does this before
  'associated_with',  -- User associates these
  
  -- Functional Links
  'enabled_by',       -- Requires this to function
  'provides',         -- Provides capability
  'input_to',         -- Input to a process
  'output_of',        -- Output of a process
  
  -- Communication Links
  'sent_to',           -- Message sent to entity
  'received_from',    -- Message received from
  'mentioned',        -- Entity was mentioned
  
  -- Value Links
  'costs',            -- This costs money
  'earned_from',      -- Money earned from
  'saved_by',         -- Money saved by
  'invested_in'      -- Investment in
);
```

### 4.2 Link Schema

```sql
CREATE TABLE links (
  -- Identity
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  -- Endpoints
  source_node_id  UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
  target_node_id  UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
  
  -- Classification
  type            link_type NOT NULL,
  
  -- Strength & Weight
  weight          FLOAT NOT NULL DEFAULT 0.5,      -- 0-1, link strength
  confidence      FLOAT NOT NULL DEFAULT 1.0,     -- 0-1, how confident we are
  semantic_distance FLOAT,                          -- Computed semantic distance
  
  -- Temporal
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  last_pulsed_at  TIMESTAMPTZ,                     -- Last time data flowed through
  
  -- Provenance
  source_device   UUID REFERENCES devices(id),
  source_session  UUID REFERENCES sessions(id),
  is_ai_generated BOOLEAN NOT NULL DEFAULT FALSE,  -- Created by AI vs user
  
  -- Lifecycle
  is_active       BOOLEAN NOT NULL DEFAULT TRUE,
  expires_at      TIMESTAMPTZ,                     -- Optional expiration
  
  -- Visualization
  color           TEXT,                            -- Override default color
  curve_strength  FLOAT DEFAULT 0,                 -- For 3D curve bending
  
  -- Constraints
  UNIQUE(tenant_id, source_node_id, target_node_id, type),
  CHECK (source_node_id != target_node_id),
  CHECK (weight >= 0 AND weight <= 1),
  CHECK (confidence >= 0 AND confidence <= 1)
);

-- Indexes
CREATE INDEX idx_links_source ON links (tenant_id, source_node_id);
CREATE INDEX idx_links_target ON links (tenant_id, target_node_id);
CREATE INDEX idx_links_type ON links (tenant_id, type);
CREATE INDEX idx_links_weight ON links (tenant_id, weight DESC);
```

---

## 5. Semantic Distance & Auto-Linking

### 5.1 Distance Calculation

```typescript
interface SemanticDistance {
  cosine: number;      // Vector similarity (0-1, higher = more similar)
  temporal: number;   // Time decay factor (0-1)
  behavioral: number; // Co-occurrence score (0-1)
  combined: number;   // Weighted combination
}

function calculateSemanticDistance(nodeA: Node, nodeB: Node): SemanticDistance {
  // 1. Cosine distance from embeddings
  const cosine = 1 - cosineDistance(nodeA.embedding, nodeB.embedding);
  
  // 2. Temporal proximity (exponential decay)
  const hoursSince = (Date.now() - nodeB.created_at) / (1000 * 60 * 60);
  const temporal = Math.exp(-hoursSince / (24 * 7)); // 1 week half-life
  
  // 3. Behavioral co-occurrence
  const behavioral = calculateCoOccurrence(nodeA, nodeB);
  
  // 4. Weighted combination
  const combined = (
    cosine * 0.5 +      // Embedding similarity
    temporal * 0.3 +  // Recency
    behavioral * 0.2   // Co-occurrence
  );
  
  return { cosine, temporal, behavioral, combined };
}
```

### 5.2 Auto-Linking Algorithm

```typescript
async function autoLinkNewNode(newNode: Node): Promise<Link[]> {
  const createdLinks: Link[] = [];
  
  // 1. Find nodes within semantic radius
  const nearbyNodes = await findNearbyNodes(newNode, {
    maxDistance: 0.7,
    limit: 20,
    excludeTypes: ['user', 'device']  // Don't link to system nodes
  });
  
  for (const candidate of nearbyNodes) {
    const distance = calculateSemanticDistance(newNode, candidate.node);
    
    // 2. Decide link type based on relationships
    const linkType = determineLinkType(newNode, candidate.node);
    
    // 3. Calculate link weight
    const weight = distance.combined * candidate.proximity;
    
    // 4. Only create if above threshold
    if (weight >= LINK_THRESHOLD) {
      const link = await createLink({
        source: newNode.id,
        target: candidate.node.id,
        type: linkType,
        weight: weight,
        semantic_distance: 1 - distance.combined,
        is_ai_generated: true
      });
      
      createdLinks.push(link);
    }
  }
  
  // 5. Create inverse links for bidirectional relationships
  for (const link of createdLinks) {
    if (BIDIRECTIONAL_TYPES.includes(link.type)) {
      await createLink({
        source: link.target,
        target: link.source,
        type: getInverseType(link.type),
        weight: link.weight,
        is_ai_generated: true
      });
    }
  }
  
  return createdLinks;
}

const LINK_THRESHOLD = 0.3;
const BIDIRECTIONAL_TYPES = ['related_to', 'similar_to', 'associated_with', 'member_of'];
```

### 5.3 Self-Evolving Graph Logic

```typescript
// Pattern: "User typically does X after Y"
interface BehavioralPattern {
  trigger: Node;
  response: Node;
  frequency: number;      // How often this happens
  confidence: number;     // Statistical confidence
  lastSeen: Date;
}

// Detect patterns in user behavior
async function detectPatterns(tenantId: string): Promise<BehavioralPattern[]> {
  const patterns: BehavioralPattern[] = [];
  
  // Query: Find pairs of events that co-occur within 2-hour windows
  const result = await supabase.rpc('detect_cooccurrence_patterns', {
    p_tenant_id: tenantId,
    p_time_window_hours: 2,
    p_min_frequency: 3
  });
  
  for (const pattern of result.data) {
    // Only keep statistically significant patterns
    if (pattern.confidence >= 0.7) {
      patterns.push({
        trigger: pattern.before_node,
        response: pattern.after_node,
        frequency: pattern.count,
        confidence: pattern.confidence,
        lastSeen: pattern.last_timestamp
      });
      
      // Auto-create link if pattern is strong
      if (pattern.confidence >= 0.85) {
        await createLink({
          source: pattern.before_node,
          target: pattern.after_node,
          type: 'followed_by',
          weight: pattern.confidence,
          is_ai_generated: true
        });
      }
    }
  }
  
  return patterns;
}
```

---

## 6. Complete Database Schema (Supabase)

### 6.1 Core Tables

```sql
-- ============================================================
-- TENANT ISOLATION
-- ============================================================

CREATE TABLE tenants (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  email           TEXT UNIQUE NOT NULL,
  name            TEXT NOT NULL,
  plan            TEXT NOT NULL DEFAULT 'free',  -- free, pro, enterprise
  settings        JSONB DEFAULT '{}',
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================
-- DEVICE MANAGEMENT (ESP32 Devices)
-- ============================================================

CREATE TABLE devices (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  -- Identity
  device_type     TEXT NOT NULL,  -- 'esp32s3', 'esp32c3', etc.
  mac_address     TEXT UNIQUE,
  device_name     TEXT NOT NULL,
  
  -- Pairing
  pair_code       TEXT,
  pair_expires_at TIMESTAMPTZ,
  is_paired       BOOLEAN DEFAULT FALSE,
  
  -- State
  last_seen_at    TIMESTAMPTZ,
  battery_level   INTEGER,
  firmware_version TEXT,
  
  -- ESP32 SQLite sync
  sqlite_version  INTEGER DEFAULT 0,  -- Sync version counter
  last_sync_at    TIMESTAMPTZ,
  
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  updated_at      TIMESTAMPTZ DEFAULT NOW(),
  
  UNIQUE(tenant_id, mac_address)
);

-- ============================================================
-- NODES (3D Knowledge Graph)
-- ============================================================

CREATE TABLE nodes (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  type            TEXT NOT NULL,
  subtype         TEXT,
  name            TEXT NOT NULL,
  description     TEXT,
  content         JSONB DEFAULT '{}',
  
  -- Vector embedding for semantic search
  embedding       VECTOR(1536),
  
  -- 3D Position in Knowledge Graph
  pos_x           FLOAT NOT NULL DEFAULT 50,
  pos_y           FLOAT NOT NULL DEFAULT 500,
  pos_z           FLOAT NOT NULL DEFAULT 50,
  
  -- Metadata
  metadata        JSONB DEFAULT '{}',
  
  -- Timestamps
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  updated_at      TIMESTAMPTZ DEFAULT NOW(),
  accessed_at     TIMESTAMPTZ,
  
  -- State
  is_active       BOOLEAN DEFAULT TRUE,
  is_pinned       BOOLEAN DEFAULT FALSE,
  is_favorite     BOOLEAN DEFAULT FALSE,
  
  -- Provenance
  source_device   UUID REFERENCES devices(id),
  source_session  UUID,  -- References sessions(id)
  
  -- Statistics
  activation_count INT DEFAULT 0,
  link_count      Int DEFAULT 0,
  pulse_strength  FLOAT DEFAULT 0,
  
  UNIQUE(tenant_id, type, name)
);

-- ============================================================
-- LINKS (Edges between Nodes)
-- ============================================================

CREATE TABLE links (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  source_node_id  UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
  target_node_id  UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
  
  type            TEXT NOT NULL,
  
  -- Link properties
  weight          FLOAT DEFAULT 0.5,
  confidence      FLOAT DEFAULT 1.0,
  semantic_distance FLOAT,
  
  -- Temporal
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  last_pulsed_at  TIMESTAMPTZ,
  
  -- Provenance
  source_device   UUID REFERENCES devices(id),
  source_session  UUID,
  is_ai_generated BOOLEAN DEFAULT FALSE,
  
  -- State
  is_active       BOOLEAN DEFAULT TRUE,
  expires_at      TIMESTAMPTZ,
  
  -- Visualization
  color           TEXT,
  curve_strength  FLOAT DEFAULT 0,
  
  UNIQUE(tenant_id, source_node_id, target_node_id, type),
  CHECK (source_node_id != target_node_id),
  CHECK (weight >= 0 AND weight <= 1)
);

-- ============================================================
-- SESSIONS (Conversation Sessions)
-- ============================================================

CREATE TABLE sessions (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  device_id       UUID REFERENCES devices(id),
  
  -- Session info
  session_type    TEXT NOT NULL,  -- 'voice', 'mqtt', 'telegram', 'web'
  status          TEXT DEFAULT 'active',  -- active, completed, archived
  
  -- Context
  context_snapshot JSONB DEFAULT '{}',
  active_nodes    UUID[],        -- Nodes currently in context
  pinned_nodes    UUID[],
  
  -- Metrics
  message_count   INT DEFAULT 0,
  tool_calls      INT DEFAULT 0,
  
  -- Timestamps
  started_at      TIMESTAMPTZ DEFAULT NOW(),
  ended_at        TIMESTAMPTZ,
  last_activity_at TIMESTAMPTZ DEFAULT NOW(),
  
  UNIQUE(tenant_id, id)
);

-- ============================================================
-- PULSES (Real-time Inference Events)
-- ============================================================

CREATE TABLE pulses (
  id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  -- Pulse source
  device_id       UUID REFERENCES devices(id),
  session_id      UUID REFERENCES sessions(id),
  
  -- Pulse data
  pulse_type      TEXT NOT NULL,  -- 'input', 'output', 'tool_call', 'state_change'
  content         JSONB NOT NULL,
  
  -- Graph integration
  triggered_nodes  UUID[],        -- Nodes activated by this pulse
  created_links    UUID[],        -- Links created during pulse
  
  -- Timing
  latency_ms      INTEGER,        -- Time from input to response
  created_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================
-- VECTOR STORAGE (pgvector)
-- ============================================================

CREATE EXTENSION IF NOT EXISTS vector;

-- ============================================================
-- INDEXES
-- ============================================================

-- Nodes indexes
CREATE INDEX idx_nodes_tenant ON nodes (tenant_id);
CREATE INDEX idx_nodes_type ON nodes (tenant_id, type);
CREATE INDEX idx_nodes_subtype ON nodes (tenant_id, type, subtype);
CREATE INDEX idx_nodes_spatial ON nodes USING spgist (tenant_id, box(pos_x, pos_y, pos_z));
CREATE INDEX idx_nodes_embedding ON nodes USING ivfflat (embedding vector_cosine_ops)
  WITH (lists = 100);
CREATE INDEX idx_nodes_content ON nodes USING gin (content jsonb_path_ops);
CREATE INDEX idx_nodes_updated ON nodes (tenant_id, updated_at DESC);

-- Links indexes
CREATE INDEX idx_links_tenant ON links (tenant_id);
CREATE INDEX idx_links_source ON links (tenant_id, source_node_id);
CREATE INDEX idx_links_target ON links (tenant_id, target_node_id);
CREATE INDEX idx_links_type ON links (tenant_id, type);
CREATE INDEX idx_links_weight ON links (tenant_id, weight DESC);
CREATE INDEX idx_links_created ON links (tenant_id, created_at DESC);

-- Devices indexes
CREATE INDEX idx_devices_tenant ON devices (tenant_id);
CREATE INDEX idx_devices_mac ON devices (mac_address);
CREATE INDEX idx_devices_pair ON devices (pair_code) WHERE pair_code IS NOT NULL;

-- Sessions indexes
CREATE INDEX idx_sessions_tenant ON sessions (tenant_id);
CREATE INDEX idx_sessions_device ON sessions (tenant_id, device_id);
CREATE INDEX idx_sessions_status ON sessions (tenant_id, status);

-- Pulses indexes
CREATE INDEX idx_pulses_tenant ON pulses (tenant_id);
CREATE INDEX idx_pulses_device ON pulses (tenant_id, device_id);
CREATE INDEX idx_pulses_session ON pulses (tenant_id, session_id);
CREATE INDEX idx_pulses_created ON pulses (created_at DESC);
```

### 6.2 Row Level Security (RLS)

```sql
-- ============================================================
-- ENABLE RLS ON ALL TABLES
-- ============================================================

ALTER TABLE tenants ENABLE ROW LEVEL SECURITY;
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE nodes ENABLE ROW LEVEL SECURITY;
ALTER TABLE links ENABLE ROW LEVEL SECURITY;
ALTER TABLE sessions ENABLE ROW LEVEL SECURITY;
ALTER TABLE pulses ENABLE ROW LEVEL SECURITY;

-- ============================================================
-- RLS POLICIES
-- ============================================================

-- Helper function to get current user ID
CREATE OR REPLACE FUNCTION auth.uid() RETURNS UUID AS $$
  SELECT NULLIF(current_setting('request.jwt.claim_sub', true), '')::UUID;
$$ LANGUAGE SQL STABLE;

-- Helper function to get current tenant ID
CREATE OR REPLACE FUNCTION auth.tenant_id() RETURNS UUID AS $$
  SELECT NULLIF(current_setting('request.jwt.claim_tenant_id', true), '')::UUID;
$$ LANGUAGE SQL STABLE;

-- TENANTS: Users can only see their own tenant
CREATE POLICY "Users can view own tenant"
  ON tenants FOR SELECT
  USING (id = auth.tenant_id());

CREATE POLICY "Users can update own tenant"
  ON tenants FOR UPDATE
  USING (id = auth.tenant_id());

-- DEVICES: Users can only access their own devices
CREATE POLICY "Users can view own devices"
  ON devices FOR SELECT
  USING (tenant_id = auth.tenant_id());

CREATE POLICY "Users can insert own devices"
  ON devices FOR INSERT
  WITH CHECK (tenant_id = auth.tenant_id());

CREATE POLICY "Users can update own devices"
  ON devices FOR UPDATE
  USING (tenant_id = auth.tenant_id());

CREATE POLICY "Users can delete own devices"
  ON devices FOR DELETE
  USING (tenant_id = auth.tenant_id());

-- NODES: Strict tenant isolation
CREATE POLICY "Users can view own nodes"
  ON nodes FOR SELECT
  USING (tenant_id = auth.tenant_id());

CREATE POLICY "Users can insert own nodes"
  ON nodes FOR INSERT
  WITH CHECK (tenant_id = auth.tenant_id());

CREATE POLICY "Users can update own nodes"
  ON nodes FOR UPDATE
  USING (tenant_id = auth.tenant_id());

CREATE POLICY "Users can delete own nodes"
  ON nodes FOR DELETE
  USING (tenant_id = auth.tenant_id());

-- LINKS: Strict tenant isolation
CREATE POLICY "Users can view own links"
  ON links FOR SELECT
  USING (tenant_id = auth.tenant_id());

CREATE POLICY "Users can insert own links"
  ON links FOR INSERT
  WITH CHECK (tenant_id = auth.tenant_id());

CREATE POLICY "Users can update own links"
  ON links FOR UPDATE
  USING (tenant_id = auth.tenant_id());

CREATE POLICY "Users can delete own links"
  ON links FOR DELETE
  USING (tenant_id = auth.tenant_id());

-- Block cross-tenant link creation
CREATE POLICY "Links must stay within tenant"
  ON links FOR ALL
  USING (
    tenant_id = auth.tenant_id() AND
    EXISTS (SELECT 1 FROM nodes n WHERE n.id = links.source_node_id AND n.tenant_id = auth.tenant_id()) AND
    EXISTS (SELECT 1 FROM nodes n WHERE n.id = links.target_node_id AND n.tenant_id = auth.tenant_id())
  );

-- SESSIONS: Tenant isolation
CREATE POLICY "Users can manage own sessions"
  ON sessions FOR ALL
  USING (tenant_id = auth.tenant_id())
  WITH CHECK (tenant_id = auth.tenant_id());

-- PULSES: Tenant isolation
CREATE POLICY "Users can manage own pulses"
  ON pulses FOR ALL
  USING (tenant_id = auth.tenant_id())
  WITH CHECK (tenant_id = auth.tenant_id());
```

---

## 7. Real-time Sync Architecture

### 7.1 Sync Protocol

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         NEURON LINK SYNC PROTOCOL                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   ESP32 (SQLite)  ←───────────────────────→  Supabase (PostgreSQL)      │
│                                                                          │
│   ┌─────────────┐         MQTT          ┌─────────────┐                 │
│   │   L1 Cache  │  ──────────────────→  │  Cloud DB   │                 │
│   │   SQLite    │     Delta Sync        │  PostgreSQL │                 │
│   │             │  ←──────────────────  │             │                 │
│   │  - Nodes    │     Full Sync         │  - Nodes    │                 │
│   │  - Links    │     (periodic)        │  - Links    │                 │
│   │  - Pulses   │                       │  - Sessions │                 │
│   └─────────────┘                       └─────────────┘                 │
│                                                                          │
│   ┌─────────────────────────────────────────────────────────────────┐     │
│   │                      SYNC FLOW                                  │     │
│   │                                                                  │     │
│   │  1. Device boots → Fetch full graph from cloud                  │     │
│   │  2. Device processes input → Updates SQLite                     │     │
│   │  3. Device publishes pulse → MQTT to cloud                      │     │
│   │  4. Cloud receives → Updates PostgreSQL                         │     │
│   │  5. Supabase Realtime → Pushes to all connected clients          │     │
│   │  6. Web dashboard receives → Animates 3D graph                  │     │
│   │                                                                  │     │
│   └─────────────────────────────────────────────────────────────────┘     │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 7.2 ESP32 SQLite Schema

```sql
-- L1 Cache Schema for ESP32 (SQLite)
-- Mirrors Supabase schema with additional local metadata

CREATE TABLE nodes (
  id              TEXT PRIMARY KEY,           -- UUID as text
  type            TEXT NOT NULL,
  subtype         TEXT,
  name            TEXT NOT NULL,
  description     TEXT,
  content         TEXT,                        -- JSON string
  pos_x           REAL NOT NULL DEFAULT 50,
  pos_y           REAL NOT NULL DEFAULT 500,
  pos_z           REAL NOT NULL DEFAULT 50,
  metadata        TEXT,                        -- JSON string
  created_at      TEXT,                        -- ISO8601
  updated_at      TEXT,
  is_active       INTEGER DEFAULT 1,
  is_pinned       INTEGER DEFAULT 0,
  is_favorite     INTEGER DEFAULT 0,
  activation_count INTEGER DEFAULT 0,
  link_count      INTEGER DEFAULT 0,
  pulse_strength  REAL DEFAULT 0,
  sync_version    INTEGER DEFAULT 0,          -- Incremented on change
  cloud_synced    INTEGER DEFAULT 0           -- 1 = synced to cloud
);

CREATE TABLE links (
  id              TEXT PRIMARY KEY,
  source_node_id  TEXT NOT NULL,
  target_node_id  TEXT NOT NULL,
  type            TEXT NOT NULL,
  weight          REAL DEFAULT 0.5,
  confidence      REAL DEFAULT 1.0,
  created_at      TEXT,
  is_active       INTEGER DEFAULT 1,
  sync_version    INTEGER DEFAULT 0,
  cloud_synced    INTEGER DEFAULT 0,
  FOREIGN KEY (source_node_id) REFERENCES nodes(id),
  FOREIGN KEY (target_node_id) REFERENCES nodes(id)
);

CREATE TABLE sync_log (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  table_name      TEXT NOT NULL,
  record_id       TEXT NOT NULL,
  operation       TEXT NOT NULL,    -- INSERT, UPDATE, DELETE
  old_data        TEXT,              -- JSON of old record
  new_data        TEXT,              -- JSON of new record
  created_at      TEXT DEFAULT (datetime('now')),
  synced          INTEGER DEFAULT 0
);

CREATE TABLE device_state (
  key             TEXT PRIMARY KEY,
  value           TEXT,
  updated_at      TEXT DEFAULT (datetime('now'))
);

-- Indexes
CREATE INDEX idx_nodes_type ON nodes(type);
CREATE INDEX idx_nodes_sync ON nodes(cloud_synced);
CREATE INDEX idx_links_source ON links(source_node_id);
CREATE INDEX idx_links_target ON links(target_node_id);
CREATE INDEX idx_links_sync ON links(cloud_synced);
CREATE INDEX idx_sync_log ON sync_log(synced, created_at);
```

### 7.3 Sync API Endpoints

```typescript
// API Routes for Neuron Link Sync

// POST /api/neuron/sync/delta
// Sync changes since last sync
interface DeltaSyncRequest {
  deviceId: string;
  sinceVersion: number;
  nodes: NodeDelta[];
  links: LinkDelta[];
}

interface DeltaSyncResponse {
  serverVersion: number;
  nodes: Node[];
  links: Link[];
  deletedNodeIds: string[];
  deletedLinkIds: string[];
}

// POST /api/neuron/sync/full
// Full graph sync (on device boot)
interface FullSyncRequest {
  deviceId: string;
  since: string;  // ISO timestamp
}

interface FullSyncResponse {
  version: number;
  nodes: Node[];
  links: Link[];
}

// POST /api/neuron/pulse
// Record a real-time inference pulse
interface PulseRequest {
  deviceId: string;
  sessionId: string;
  pulseType: 'input' | 'output' | 'tool_call' | 'state_change';
  content: Record<string, any>;
  triggeredNodes: string[];
  latencyMs: number;
}

// GET /api/neuron/graph
// Get full graph for visualization
interface GraphQuery {
  tenantId: string;
  depth?: number;
  nodeTypes?: string[];
  centerNodeId?: string;
}

// WebSocket /api/neuron/realtime
// Real-time updates via Supabase Realtime
```

---

## 8. 3D Visualization Model

### 8.1 React-Force-Graph-3D Integration

```typescript
interface GraphVisualization {
  nodes: VisualizationNode[];
  links: VisualizationLink[];
  config: GraphConfig;
}

interface VisualizationNode {
  id: string;
  name: string;
  type: string;
  subtype: string;
  x: number;
  y: number;
  z: number;
  
  // Visual properties
  color: string;
  size: number;          // Based on activation_count
  opacity: number;       // Based on pulse_strength
  label: string;
  icon?: string;         // Emoji or icon name
  
  // State
  isActive: boolean;
  isPinned: boolean;
  isFavorite: boolean;
  
  // Interactions
  onClick?: () => void;
  onHover?: () => void;
}

interface VisualizationLink {
  id: string;
  source: string;
  target: string;
  type: string;
  weight: number;
  
  // Visual properties
  color: string;
  opacity: number;       // Based on weight
  width: number;         // Based on weight
  curved: boolean;
  curveStrength: number;
  
  // Animation
  animated: boolean;      // Show particle flow
  particleSpeed: number;
}

interface GraphConfig {
  // Physics
  gravity: number;
  linkDistance: number;
  chargeStrength: number;
  
  // Visuals
  backgroundColor: string;
  nodeColorScheme: Record<string, string>;
  linkColorScheme: Record<string, string>;
  
  // Controls
  enableZoom: boolean;
  enableRotate: boolean;
  enablePan: boolean;
  
  // Performance
  maxNodes: number;
  lodThreshold: number;  // Distance at which to reduce detail
}
```

### 8.2 Node Color by Domain

```typescript
const DOMAIN_COLORS: Record<string, string> = {
  // System (Y: 0-99)
  'system': '#607D8B',
  'device': '#78909C',
  'user': '#455A64',
  
  // Voice (Y: 100-199)
  'voice': '#03A9F4',
  'input': '#29B6F6',
  
  // Context (Y: 200-299)
  'context': '#7C4DFF',
  'session': '#651FFF',
  'pulse': '#AA00FF',
  
  // Finance (Y: 300-399)
  'finance': '#4CAF50',
  'transaction': '#66BB6A',
  'budget': '#43A047',
  'investment': '#388E3C',
  
  // Health (Y: 400-499)
  'health': '#F44336',
  'exercise': '#EF5350',
  'nutrition': '#E57373',
  'sleep': '#D32F2F',
  
  // Home (Y: 500-599)
  'home': '#FF9800',
  'device_iot': '#FFA726',
  'room': '#FFB74D',
  
  // Social (Y: 600-699)
  'social': '#E91E63',
  'contact': '#EC407A',
  
  // Knowledge (Y: 700-799)
  'knowledge': '#2196F3',
  'document': '#42A5F5',
  'fact': '#1E88E5',
  
  // Creative (Y: 800-899)
  'creative': '#9C27B0',
  'project': '#AB47BC',
  
  // Memory (Y: 900-999)
  'memory': '#9E9E9E',
  'episodic': '#BDBDBD',
  'semantic': '#757575'
};

const LINK_COLORS: Record<string, string> = {
  'owns': '#4CAF50',
  'related_to': '#90A4AE',
  'similar_to': '#B0BEC5',
  'caused_by': '#FF5722',
  'triggered': '#FF9800',
  'followed_by': '#FFC107',
  'preceded_by': '#FFD54F',
  'costs': '#F44336',
  'earned_from': '#4CAF50',
  'input_to': '#2196F3',
  'output_of': '#03A9F4',
  'sent_to': '#9C27B0',
  'received_from': '#E91E63'
};
```

### 8.3 Particle Animation System

```typescript
// Particle flow on active links
interface Particle {
  id: string;
  linkId: string;
  progress: number;       // 0-1 position along link
  speed: number;
  size: number;
  color: string;
  opacity: number;
}

function updateParticles(particles: Particle[], deltaTime: number): Particle[] {
  return particles.map(p => {
    const newProgress = p.progress + (p.speed * deltaTime);
    
    // Reset when reaching end
    if (newProgress >= 1) {
      return { ...p, progress: 0 };
    }
    
    return { ...p, progress: newProgress };
  });
}

function renderParticle(
  ctx: CanvasRenderingContext2D,
  particle: Particle,
  sourcePos: Vector3D,
  targetPos: Vector3D,
  curveStrength: number
): void {
  // Calculate position along bezier curve
  const pos = lerp3D(
    sourcePos,
    targetPos,
    particle.progress,
    curveStrength
  );
  
  // Render glow effect
  const gradient = ctx.createRadialGradient(
    pos.x, pos.y, 0,
    pos.x, pos.y, particle.size * 3
  );
  gradient.addColorStop(0, particle.color);
  gradient.addColorStop(1, 'transparent');
  
  ctx.globalAlpha = particle.opacity;
  ctx.fillStyle = gradient;
  ctx.beginPath();
  ctx.arc(pos.x, pos.y, particle.size * 3, 0, Math.PI * 2);
  ctx.fill();
}
```

---

## 9. Skill Node Architecture

### 9.1 Skill Node Definition

```typescript
interface SkillNode {
  id: string;
  name: string;           // e.g., "Finance_Skill"
  category: string;       // e.g., "finance", "health", "home"
  
  // Capabilities
  inputTypes: string[];   // What inputs it accepts
  outputTypes: string[];  // What outputs it produces
  capabilities: string[]; // Natural language description of capabilities
  
  // Integration
  connectedNodes: string[];  // Nodes that provide inputs
  targetNodes: string[];     // Nodes that receive outputs
  
  // Configuration
  isActive: boolean;
  settings: Record<string, any>;
  
  // Statistics
  invocationCount: number;
  successRate: number;
  avgLatencyMs: number;
}

const DEFAULT_SKILLS: SkillNode[] = [
  {
    id: 'skill-finance',
    name: 'Finance_Skill',
    category: 'finance',
    inputTypes: ['transaction', 'voice_input', 'budget_query'],
    outputTypes: ['balance_update', 'budget_alert', 'spending_insight'],
    capabilities: [
      'Track expenses and income',
      'Manage budgets and categories',
      'Generate spending insights',
      'Alert on unusual transactions'
    ],
    connectedNodes: ['node-voice-input', 'node-finance-account'],
    targetNodes: ['node-finance-dashboard', 'node-budget-alerts'],
    isActive: true,
    settings: {
      currency: 'USD',
      autoCategorize: true,
      alertThreshold: 0.2  // Alert when 80% of budget used
    },
    invocationCount: 0,
    successRate: 1.0,
    avgLatencyMs: 0
  },
  {
    id: 'skill-health',
    name: 'Health_Skill',
    category: 'health',
    inputTypes: ['exercise_log', 'meal_log', 'sleep_data'],
    outputTypes: ['health_insight', 'fitness_goal_update', 'wellness_alert'],
    capabilities: [
      'Track workouts and exercise',
      'Monitor nutrition intake',
      'Analyze sleep patterns',
      'Provide health recommendations'
    ],
    connectedNodes: ['node-voice-input', 'node-health-wearable'],
    targetNodes: ['node-health-dashboard', 'node-fitness-goals'],
    isActive: true,
    settings: {
      dailyCalorieGoal: 2000,
      dailyStepGoal: 10000,
      sleepTargetHours: 8
    },
    invocationCount: 0,
    successRate: 1.0,
    avgLatencyMs: 0
  },
  {
    id: 'skill-home',
    name: 'SmartHome_Skill',
    category: 'home',
    inputTypes: ['voice_command', 'schedule', 'sensor_data'],
    outputTypes: ['device_control', 'scene_activation', 'automation_trigger'],
    capabilities: [
      'Control smart home devices',
      'Manage automation scenes',
      'React to sensor events',
      'Optimize energy usage'
    ],
    connectedNodes: ['node-voice-input', 'node-iot-hub'],
    targetNodes: ['node-home-devices', 'node-home-scenes'],
    isActive: true,
    settings: {
      defaultScene: 'evening',
      energyOptimization: true
    },
    invocationCount: 0,
    successRate: 1.0,
    avgLatencyMs: 0
  },
  {
    id: 'skill-memory',
    name: 'Memory_Skill',
    category: 'knowledge',
    inputTypes: ['conversation', 'fact', 'preference'],
    outputTypes: ['memory_recall', 'context_injection', 'insight_generation'],
    capabilities: [
      'Store episodic memories',
      'Maintain semantic knowledge',
      'Inject relevant context',
      'Generate insights from patterns'
    ],
    connectedNodes: ['node-voice-input', 'node-context'],
    targetNodes: ['node-long-term-memory', 'node-insights'],
    isActive: true,
    settings: {
      retentionDays: 90,
      importanceThreshold: 0.5,
      patternDetectionEnabled: true
    },
    invocationCount: 0,
    successRate: 1.0,
    avgLatencyMs: 0
  }
];
```

### 9.2 Skill Node Graph Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    SKILL NODE PROCESSING FLOW                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   ┌──────────────┐                                                       │
│   │  Voice_Input │                                                       │
│   │     Node     │                                                       │
│   └──────┬───────┘                                                       │
│          │                                                               │
│          │ (triggered_by)                                                │
│          ▼                                                               │
│   ┌──────────────┐      ┌──────────────┐      ┌──────────────┐          │
│   │ Finance_Skill│─────→│   Budgets    │─────→│  Dashboard   │          │
│   │    Node      │      │     Node     │      │     Node     │          │
│   └──────┬───────┘      └──────────────┘      └──────────────┘          │
│          │                                                               │
│          │ (output_of)                                                   │
│          ▼                                                               │
│   ┌──────────────┐                                                       │
│   │ Transaction  │                                                       │
│   │     Node     │ ←─── (related_to) ───→  ┌──────────────┐            │
│   └──────────────┘                         │    Tags      │            │
│                                             │   Nodes      │            │
│                                             └──────────────┘            │
│                                                                          │
│   PARTICLE ANIMATION: Data flows from Voice → Skill → Output            │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 10. Implementation Phases

### Phase 1: Core Graph Schema (This Document)
- [x] Node taxonomy design
- [x] Link taxonomy design
- [x] Spatial coordinate system
- [x] RLS policies for multi-tenant
- [x] Supabase schema migration
- [x] ESP32 SQLite schema

### Phase 2: Neuron Link API
- [ ] Supabase Edge Functions for sync
- [ ] Delta sync protocol implementation
- [ ] Full sync with pagination
- [ ] MQTT integration

### Phase 3: Real-time Dashboard
- [ ] Next.js 14 App Router setup
- [ ] react-force-graph-3d integration
- [ ] Supabase Realtime subscriptions
- [ ] Particle animation system

### Phase 4: AI Self-Evolution
- [ ] Pattern detection SQL functions
- [ ] Auto-linking algorithm
- [ ] Claude/GPT integration for insights
- [ ] Behavioral link creation

### Phase 5: Edge Integration
- [ ] ESP32 MQTT client
- [ ] SQLite ↔ Cloud sync
- [ ] Pair-code authentication
- [ ] Battery-aware sync scheduling

---

## 11. Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Voice → 3D Vibration Latency | < 500ms | ESP32 input → WebSocket → Client |
| Graph Load Time | < 2s | For 1000 nodes |
| Semantic Search | < 100ms | Vector similarity query |
| Delta Sync | < 200ms | MQTT → Supabase → Realtime |
| Link Creation | < 50ms | Auto-linking pipeline |
| RTT (Cloud Sync) | < 300ms | ESP32 → Supabase → ESP32 |

---

## 12. Appendix: Example Graph State

```json
{
  "tenantId": "uuid-tenant-001",
  "nodes": [
    {
      "id": "node-user-001",
      "type": "user",
      "name": "John Doe",
      "pos_x": 50,
      "pos_y": 50,
      "pos_z": 50,
      "content": { "email": "john@example.com", "preferences": {} }
    },
    {
      "id": "node-esp32-001",
      "type": "device",
      "subtype": "esp32s3",
      "name": "Living Room ESP",
      "pos_x": 30,
      "pos_y": 50,
      "pos_z": 80,
      "source_device": null
    },
    {
      "id": "node-skill-finance",
      "type": "skill",
      "subtype": "finance_accounting",
      "name": "Finance_Skill",
      "pos_x": 85,
      "pos_y": 350,
      "pos_z": 45,
      "content": {
        "capabilities": ["track_expenses", "budget_alerts"],
        "settings": { "currency": "USD" }
      }
    },
    {
      "id": "node-tx-001",
      "type": "transaction",
      "name": "Coffee Purchase",
      "pos_x": 72,
      "pos_y": 310,
      "pos_z": 25,
      "content": {
        "amount": 5.50,
        "category": "food",
        "merchant": "Starbucks"
      }
    }
  ],
  "links": [
    {
      "id": "link-001",
      "source": "node-user-001",
      "target": "node-esp32-001",
      "type": "owns",
      "weight": 1.0
    },
    {
      "id": "link-002",
      "source": "node-user-001",
      "target": "node-skill-finance",
      "type": "owns",
      "weight": 0.9
    },
    {
      "id": "link-003",
      "source": "node-tx-001",
      "target": "node-skill-finance",
      "type": "input_to",
      "weight": 0.8,
      "is_ai_generated": true
    },
    {
      "id": "link-004",
      "source": "node-tx-001",
      "target": "node-tag-food",
      "type": "related_to",
      "weight": 0.7,
      "is_ai_generated": true
    }
  ]
}
```

---

## 13. Hardware Configuration

### 13.1 ESP32-S3 Pinout

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-S3 DevKit Pinout                    │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  AUDIO INPUT (PDM Microphone)                       │    │
│  │  ─────────────────────────────────────────────────── │    │
│  │  • GPIO4  → DATA (PDM Data)                        │    │
│  │  • GPIO3  → CLK (PDM Clock)                        │    │
│  │  • 3.3V   → VCC                                   │    │
│  │  • GND    → GND                                   │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  AUDIO OUTPUT (I2S DAC - MAX98357A)              │    │
│  │  ─────────────────────────────────────────────────── │    │
│  │  • GPIO44 → BCLK (Bit Clock)                       │    │
│  │  • GPIO43 → LRC  (Left/Right Clock)               │    │
│  │  • GPIO42 → DIN  (Data Input)                      │    │
│  │  • 3.3V   → VCC                                   │    │
│  │  • GND    → GND                                   │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  DISPLAY (OLED 4-pin I2C)                          │    │
│  │  ─────────────────────────────────────────────────── │    │
│  │  • GPIO1  → SDA (Data)                            │    │
│  │  • GPIO2  → SCL (Clock)                           │    │
│  │  • 3.3V   → VCC                                   │    │
│  │  • GND    → GND                                   │    │
│  │  Supported: 128x64 SSD1306, SH1106                 │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │  STATUS INDICATOR                                   │    │
│  │  ─────────────────────────────────────────────────── │    │
│  │  • GPIO48 → Status LED (on-board or external)     │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 13.2 Configuration via Dashboard

Tất cả hardware configuration được quản lý qua Dashboard và sync về ESP32:

| Component | Config Location | Sync Method |
|-----------|----------------|-------------|
| **GPIO Pins** | `devices.gpio_config` JSONB | MQTT delta sync |
| **LLM Provider** | `tenants.llm_config` JSONB | MQTT delta sync |
| **Telegram Bot** | `devices.telegram_config` JSONB | MQTT delta sync |
| **MQTT Broker** | `devices.mqtt_config` JSONB | MQTT delta sync |

### 13.3 LLM Configuration (Dashboard)

```json
// tenants.llm_config structure
{
  "provider": "openai",  // openai, anthropic, openrouter, ollama, custom
  "api_key": "encrypted_key",
  "model": "gpt-4o-mini",
  "base_url": "https://api.openai.com/v1",  // for custom providers
  "temperature": 0.7,
  "max_tokens": 2048,
  "system_prompt": "You are Neutron, an AI assistant..."
}
```

### 13.4 Telegram Configuration (Dashboard)

```json
// devices.telegram_config structure
{
  "enabled": true,
  "bot_token": "encrypted_token",
  "chat_id": "123456789",
  "notifications_enabled": true
}
```

### 13.5 MQTT Configuration (Dashboard)

```json
// devices.mqtt_config structure
{
  "broker_url": "mqtts://broker.example.com:8883",
  "username": "device_username",
  "password": "encrypted_password",
  "topics": {
    "command": "espclaw/{device_id}/cmd",
    "response": "espclaw/{device_id}/response",
    "sync": "espclaw/{device_id}/sync",
    "pulse": "espclaw/{device_id}/pulse"
  },
  "qos": 1,
  "retain": false
}
```

### 13.6 GPIO Configuration (Dashboard)

```json
// devices.gpio_config structure
{
  "mic": {
    "type": "pdm",
    "data_pin": 4,
    "clk_pin": 3,
    "enabled": true
  },
  "dac": {
    "type": "i2s_max98357a",
    "bclk_pin": 44,
    "lrc_pin": 43,
    "din_pin": 42,
    "enabled": true,
    "volume": 80
  },
  "oled": {
    "type": "ssd1306",
    "sda_pin": 1,
    "scl_pin": 2,
    "width": 128,
    "height": 64,
    "enabled": true
  },
  "status_led": {
    "pin": 48,
    "inverted": false,
    "enabled": true
  }
}
```

---

## 14. Cloud Credentials

### 14.1 Supabase

| Variable | Value |
|----------|-------|
| Project URL | `https://jophbrsbsfmtgfbfrvjw.supabase.co` |
| Anon Key | `eyJhbGci...` (đã lưu trong secrets.env) |

### 14.2 HiveMQ Cloud (MQTT Broker)

| Variable | Value |
|----------|-------|
| Broker URL | `mqtts://216f9e2d8c15496ab889d41cfff20880.s1.eu.hivemq.cloud:8883` |
| Username | `myesp123` |
| Password | `by4@eQpAmSKTTCh` |

### 14.3 Security Notes

> **WARNING**: Credentials thực tế được lưu trong `secrets.env`
> - **KHÔNG commit** secrets.env vào git
> - **KHÔNG expose** Service Role Key ra client
> - **MÃ HÓA** API keys trong database (sử dụng AES-256-GCM)

---

**Document Version**: 1.1  
**Last Updated**: 2026-04-28  
**Status**: Hardware config documented
