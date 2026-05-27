-- ============================================================
-- NEUTRON-ESPClaw: 3D Knowledge Graph Migration
-- Version: 001
-- Description: Core schema for multi-tenant knowledge graph
-- ============================================================

-- Enable required extensions
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_trgm";  -- For fuzzy text search

-- Enable pgvector for semantic search
CREATE EXTENSION IF NOT EXISTS vector;

-- ============================================================
-- ENUMS
-- ============================================================

CREATE TYPE node_type AS ENUM (
  'user',
  'device',
  'skill',
  'memory',
  'tag',
  'transaction',
  'entity',
  'event',
  'goal',
  'routine',
  'insight',
  'pulse',
  'webhook',
  'session',
  'context'
);

CREATE TYPE link_type AS ENUM (
  'owns',
  'part_of',
  'member_of',
  'related_to',
  'similar_to',
  'caused_by',
  'resulted_in',
  'happened_before',
  'happened_after',
  'triggered',
  'followed_by',
  'preceded_by',
  'associated_with',
  'enabled_by',
  'provides',
  'input_to',
  'output_of',
  'sent_to',
  'received_from',
  'costs',
  'earned_from',
  'saved_by',
  'invested_in'
);

CREATE TYPE device_type AS ENUM (
  'esp32c3',
  'esp32c5',
  'esp32c6',
  'esp32s3',
  'esp32s3_sense',
  'esp32s3_plus'
);

CREATE TYPE session_type AS ENUM (
  'voice',
  'mqtt',
  'telegram',
  'serial',
  'web'
);

CREATE TYPE pulse_type AS ENUM (
  'input',
  'output',
  'tool_call',
  'state_change'
);

-- ============================================================
-- TENANTS TABLE (Multi-tenant root)
-- ============================================================

CREATE TABLE tenants (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  email           TEXT UNIQUE NOT NULL,
  name            TEXT NOT NULL,
  plan            TEXT NOT NULL DEFAULT 'free' CHECK (plan IN ('free', 'pro', 'enterprise')),
  
  -- Quotas
  max_devices     INTEGER NOT NULL DEFAULT 5,
  max_nodes       INTEGER NOT NULL DEFAULT 1000,
  max_storage_mb  INTEGER NOT NULL DEFAULT 100,
  
  -- Settings
  settings        JSONB DEFAULT '{}',
  metadata        JSONB DEFAULT '{}',
  
  -- Billing
  stripe_customer_id TEXT,
  subscription_id    TEXT,
  
  -- Timestamps
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  deleted_at      TIMESTAMPTZ
);

CREATE INDEX idx_tenants_email ON tenants (email);

-- ============================================================
-- DEVICES TABLE (ESP32 Devices)
-- ============================================================

CREATE TABLE devices (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  -- Identity
  device_type     device_type NOT NULL,
  mac_address     TEXT,
  device_name     TEXT NOT NULL,
  firmware_version TEXT,
  
  -- Pairing (6-digit code shown on device screen)
  pair_code       TEXT,
  pair_expires_at TIMESTAMPTZ,
  is_paired       BOOLEAN NOT NULL DEFAULT FALSE,
  
  -- Pairing credentials
  device_secret   TEXT,  -- Hashed secret for API auth
  
  -- Hardware info
  chip_model      TEXT,
  chip_revision   INTEGER,
  flash_size_mb   INTEGER,
  psram_size_mb   INTEGER,
  
  -- State
  last_seen_at    TIMESTAMPTZ,
  battery_level   INTEGER CHECK (battery_level >= 0 AND battery_level <= 100),
  is_online       BOOLEAN NOT NULL DEFAULT FALSE,
  
  -- Sync state
  sqlite_version   INTEGER NOT NULL DEFAULT 0,
  last_sync_at    TIMESTAMPTZ,
  sync_status     TEXT DEFAULT 'idle' CHECK (sync_status IN ('idle', 'syncing', 'error')),
  last_error      TEXT,
  
  -- Timestamps
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  
  -- Unique constraints
  UNIQUE(tenant_id, mac_address),
  UNIQUE(tenant_id, device_name)
);

CREATE INDEX idx_devices_tenant ON devices (tenant_id);
CREATE INDEX idx_devices_mac ON devices (mac_address) WHERE mac_address IS NOT NULL;
CREATE INDEX idx_devices_pair_code ON devices (pair_code) WHERE pair_code IS NOT NULL;
CREATE INDEX idx_devices_online ON devices (tenant_id, is_online);

-- ============================================================
-- NODES TABLE (3D Knowledge Graph Nodes)
-- ============================================================

CREATE TABLE nodes (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  -- Classification
  type            node_type NOT NULL,
  subtype         TEXT,
  
  -- Content
  name            TEXT NOT NULL,
  description     TEXT,
  content         JSONB NOT NULL DEFAULT '{}',
  
  -- Vector embedding for semantic search (OpenAI text-embedding-3-small: 1536 dims)
  embedding       VECTOR(1536),
  
  -- 3D Position in Knowledge Graph (0-100 for each axis)
  pos_x           FLOAT NOT NULL DEFAULT 50.0 CHECK (pos_x >= 0 AND pos_x <= 100),
  pos_y           FLOAT NOT NULL DEFAULT 500.0,  -- Domain layer (0-999)
  pos_z           FLOAT NOT NULL DEFAULT 50.0 CHECK (pos_z >= 0 AND pos_z <= 100),
  
  -- Additional metadata
  metadata        JSONB NOT NULL DEFAULT '{}',
  
  -- Timestamps
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  accessed_at     TIMESTAMPTZ,
  
  -- State flags
  is_active       BOOLEAN NOT NULL DEFAULT TRUE,
  is_pinned       BOOLEAN NOT NULL DEFAULT FALSE,
  is_favorite     BOOLEAN NOT NULL DEFAULT FALSE,
  is_deleted      BOOLEAN NOT NULL DEFAULT FALSE,
  
  -- Provenance
  source_device   UUID REFERENCES devices(id) ON DELETE SET NULL,
  source_session  UUID,  -- Will be FK after sessions table is created
  created_by      TEXT,  -- 'user', 'device', 'ai'
  
  -- Statistics
  activation_count INTEGER NOT NULL DEFAULT 0,
  link_count      INTEGER NOT NULL DEFAULT 0,
  pulse_strength  FLOAT NOT NULL DEFAULT 0.0 CHECK (pulse_strength >= 0 AND pulse_strength <= 1),
  
  -- Versioning for sync
  version         INTEGER NOT NULL DEFAULT 1,
  etag            TEXT,
  
  -- Constraints
  UNIQUE(tenant_id, type, name)
);

-- Indexes for nodes
CREATE INDEX idx_nodes_tenant ON nodes (tenant_id);
CREATE INDEX idx_nodes_type ON nodes (tenant_id, type);
CREATE INDEX idx_nodes_subtype ON nodes (tenant_id, type, subtype) WHERE subtype IS NOT NULL;
CREATE INDEX idx_nodes_spatial ON nodes (tenant_id, pos_x, pos_y, pos_z);
CREATE INDEX idx_nodes_embedding ON nodes USING ivfflat (embedding vector_cosine_ops)
  WITH (lists = 100);
CREATE INDEX idx_nodes_content ON nodes USING gin (content jsonb_path_ops);
CREATE INDEX idx_nodes_updated ON nodes (tenant_id, updated_at DESC);
CREATE INDEX idx_nodes_active ON nodes (tenant_id, is_active) WHERE is_active = TRUE;
CREATE INDEX idx_nodes_name_trgm ON nodes USING gin (name gin_trgm_ops);

-- ============================================================
-- LINKS TABLE (Edges between Nodes)
-- ============================================================

CREATE TABLE links (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  -- Endpoints
  source_node_id  UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
  target_node_id  UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
  
  -- Classification
  type            link_type NOT NULL,
  
  -- Link properties
  weight          FLOAT NOT NULL DEFAULT 0.5 CHECK (weight >= 0 AND weight <= 1),
  confidence      FLOAT NOT NULL DEFAULT 1.0 CHECK (confidence >= 0 AND confidence <= 1),
  semantic_distance FLOAT CHECK (semantic_distance >= 0 AND semantic_distance <= 1),
  
  -- Temporal
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  last_pulsed_at  TIMESTAMPTZ,
  
  -- Provenance
  source_device   UUID REFERENCES devices(id) ON DELETE SET NULL,
  source_session  UUID,
  is_ai_generated BOOLEAN NOT NULL DEFAULT FALSE,
  created_by      TEXT DEFAULT 'user',  -- 'user', 'device', 'ai'
  
  -- State
  is_active       BOOLEAN NOT NULL DEFAULT TRUE,
  is_deleted      BOOLEAN NOT NULL DEFAULT FALSE,
  expires_at      TIMESTAMPTZ,
  
  -- Visualization
  color           TEXT,
  curve_strength  FLOAT NOT NULL DEFAULT 0.0,
  
  -- Versioning
  version         INTEGER NOT NULL DEFAULT 1,
  
  -- Constraints
  UNIQUE(tenant_id, source_node_id, target_node_id, type),
  CHECK (source_node_id != target_node_id)
);

-- Indexes for links
CREATE INDEX idx_links_tenant ON links (tenant_id);
CREATE INDEX idx_links_source ON links (tenant_id, source_node_id);
CREATE INDEX idx_links_target ON links (tenant_id, target_node_id);
CREATE INDEX idx_links_type ON links (tenant_id, type);
CREATE INDEX idx_links_weight ON links (tenant_id, weight DESC);
CREATE INDEX idx_links_created ON links (tenant_id, created_at DESC);
CREATE INDEX idx_links_pulsed ON links (tenant_id, last_pulsed_at DESC) WHERE last_pulsed_at IS NOT NULL;

-- ============================================================
-- SESSIONS TABLE (Conversation Sessions)
-- ============================================================

CREATE TABLE sessions (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
  
  -- Session classification
  session_type    session_type NOT NULL,
  status          TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'paused', 'completed', 'archived')),
  
  -- Content
  title           TEXT,
  context_snapshot JSONB NOT NULL DEFAULT '{}',
  
  -- Active nodes in this session
  active_nodes    UUID[] DEFAULT '{}',
  pinned_nodes    UUID[] DEFAULT '{}',
  
  -- Metrics
  message_count   INTEGER NOT NULL DEFAULT 0,
  tool_calls      INTEGER NOT NULL DEFAULT 0,
  
  -- Timestamps
  started_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  ended_at        TIMESTAMPTZ,
  last_activity_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  
  -- Metadata
  metadata        JSONB NOT NULL DEFAULT '{}'
);

CREATE INDEX idx_sessions_tenant ON sessions (tenant_id);
CREATE INDEX idx_sessions_device ON sessions (tenant_id, device_id) WHERE device_id IS NOT NULL;
CREATE INDEX idx_sessions_status ON sessions (tenant_id, status);
CREATE INDEX idx_sessions_active ON sessions (tenant_id, status, last_activity_at DESC) 
  WHERE status = 'active';

-- Add FK now that sessions table exists
ALTER TABLE nodes 
  ADD CONSTRAINT fk_nodes_source_session 
  FOREIGN KEY (source_session) REFERENCES sessions(id) ON DELETE SET NULL;

ALTER TABLE links 
  ADD CONSTRAINT fk_links_source_session 
  FOREIGN KEY (source_session) REFERENCES sessions(id) ON DELETE SET NULL;

-- ============================================================
-- PULSES TABLE (Real-time Inference Events)
-- ============================================================

CREATE TABLE pulses (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  
  -- Pulse source
  device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
  session_id      UUID REFERENCES sessions(id) ON DELETE SET NULL,
  
  -- Pulse data
  pulse_type      pulse_type NOT NULL,
  content         JSONB NOT NULL,
  
  -- Graph integration
  triggered_nodes  UUID[] DEFAULT '{}',
  created_links    UUID[] DEFAULT '{}',
  created_nodes    UUID[] DEFAULT '{}',
  
  -- Performance
  latency_ms      INTEGER,
  processing_time_ms INTEGER,
  
  -- Timestamps
  created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_pulses_tenant ON pulses (tenant_id);
CREATE INDEX idx_pulses_device ON pulses (tenant_id, device_id) WHERE device_id IS NOT NULL;
CREATE INDEX idx_pulses_session ON pulses (tenant_id, session_id) WHERE session_id IS NOT NULL;
CREATE INDEX idx_pulses_created ON pulses (tenant_id, created_at DESC);
CREATE INDEX idx_pulses_type ON pulses (tenant_id, pulse_type);

-- ============================================================
-- AUTO-UPDATE TRIGGER FUNCTIONS
-- ============================================================

CREATE OR REPLACE FUNCTION update_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Apply updated_at triggers
CREATE TRIGGER update_tenants_updated_at
  BEFORE UPDATE ON tenants
  FOR EACH ROW EXECUTE FUNCTION update_updated_at();

CREATE TRIGGER update_devices_updated_at
  BEFORE UPDATE ON devices
  FOR EACH ROW EXECUTE FUNCTION update_updated_at();

CREATE TRIGGER update_nodes_updated_at
  BEFORE UPDATE ON nodes
  FOR EACH ROW EXECUTE FUNCTION update_updated_at();

CREATE TRIGGER update_sessions_updated_at
  BEFORE UPDATE ON sessions
  FOR EACH ROW EXECUTE FUNCTION update_updated_at();

-- ============================================================
-- UTILITY FUNCTIONS
-- ============================================================

-- Generate pair code
CREATE OR REPLACE FUNCTION generate_pair_code()
RETURNS TEXT AS $$
DECLARE
  code TEXT;
BEGIN
  code := LPAD(FLOOR(RANDOM() * 1000000)::TEXT, 6, '0');
  RETURN code;
END;
$$ LANGUAGE plpgsql;

-- Generate device secret (for API authentication)
CREATE OR REPLACE FUNCTION generate_device_secret()
RETURNS TEXT AS $$
BEGIN
  RETURN encode(sha256(random()::TEXT::BYTEA), 'hex');
END;
$$ LANGUAGE plpgsql;

-- Calculate spatial distance between two nodes
CREATE OR REPLACE FUNCTION calculate_spatial_distance(
  node1_pos_x FLOAT,
  node1_pos_y FLOAT,
  node1_pos_z FLOAT,
  node2_pos_x FLOAT,
  node2_pos_y FLOAT,
  node2_pos_z FLOAT
)
RETURNS FLOAT AS $$
BEGIN
  RETURN SQRT(
    POWER(node2_pos_x - node1_pos_x, 2) +
    POWER(node2_pos_y - node1_pos_y, 2) +
    POWER(node2_pos_z - node1_pos_z, 2)
  );
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- Get nodes within spatial radius
CREATE OR REPLACE FUNCTION get_nearby_nodes(
  p_tenant_id UUID,
  p_node_id UUID,
  p_radius FLOAT DEFAULT 20.0,
  p_limit INTEGER DEFAULT 20
)
RETURNS TABLE (
  id UUID,
  name TEXT,
  type node_type,
  distance FLOAT
) AS $$
DECLARE
  v_pos_x FLOAT;
  v_pos_y FLOAT;
  v_pos_z FLOAT;
BEGIN
  -- Get source node position
  SELECT pos_x, pos_y, pos_z INTO v_pos_x, v_pos_y, v_pos_z
  FROM nodes
  WHERE id = p_node_id AND tenant_id = p_tenant_id;
  
  IF v_pos_x IS NULL THEN
    RETURN;
  END IF;
  
  RETURN QUERY
  SELECT
    n.id,
    n.name,
    n.type,
    calculate_spatial_distance(v_pos_x, v_pos_y, v_pos_z, n.pos_x, n.pos_y, n.pos_z) as distance
  FROM nodes n
  WHERE n.tenant_id = p_tenant_id
    AND n.id != p_node_id
    AND n.is_active = TRUE
    AND calculate_spatial_distance(v_pos_x, v_pos_y, v_pos_z, n.pos_x, n.pos_y, n.pos_z) <= p_radius
  ORDER BY distance ASC
  LIMIT p_limit;
END;
$$ LANGUAGE plpgsql;

-- ============================================================
-- METADATA TABLES
-- ============================================================

-- Store graph statistics per tenant
CREATE TABLE graph_stats (
  tenant_id       UUID PRIMARY KEY REFERENCES tenants(id) ON DELETE CASCADE,
  
  total_nodes     INTEGER NOT NULL DEFAULT 0,
  total_links     INTEGER NOT NULL DEFAULT 0,
  
  -- Node type breakdown
  nodes_by_type   JSONB NOT NULL DEFAULT '{}',
  
  -- Link type breakdown
  links_by_type   JSONB NOT NULL DEFAULT '{}',
  
  -- Domain breakdown (by Y position)
  nodes_by_domain JSONB NOT NULL DEFAULT '{}',
  
  -- Last computed
  computed_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Device sync log for debugging
CREATE TABLE sync_log (
  id              BIGSERIAL PRIMARY KEY,
  device_id       UUID NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  
  sync_type       TEXT NOT NULL CHECK (sync_type IN ('full', 'delta', 'pulse')),
  direction       TEXT NOT NULL CHECK (direction IN ('upload', 'download')),
  
  -- Counts
  nodes_sent      INTEGER DEFAULT 0,
  nodes_received  INTEGER DEFAULT 0,
  links_sent      INTEGER DEFAULT 0,
  links_received  INTEGER DEFAULT 0,
  
  -- Timing
  started_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  completed_at    TIMESTAMPTZ,
  duration_ms    INTEGER,
  
  -- Status
  status          TEXT DEFAULT 'in_progress' CHECK (status IN ('in_progress', 'success', 'error')),
  error_message   TEXT,
  bytes_transferred INTEGER DEFAULT 0
);

CREATE INDEX idx_sync_log_device ON sync_log (device_id, started_at DESC);

-- ============================================================
-- COMMENTS
-- ============================================================

COMMENT ON TABLE nodes IS '3D Knowledge Graph nodes - every entity in the system is a node';
COMMENT ON TABLE links IS 'Edges between nodes - defines relationships and data flow';
COMMENT ON COLUMN nodes.pos_x IS 'X position (0-100) - represents importance/attention';
COMMENT ON COLUMN nodes.pos_y IS 'Y position (0-999) - represents domain/layer';
COMMENT ON COLUMN nodes.pos_z IS 'Z position (0-100) - represents time/reciency';
COMMENT ON COLUMN nodes.embedding IS 'Vector embedding for semantic similarity search';
COMMENT ON COLUMN nodes.pulse_strength IS '0-1 value for 3D visualization particle intensity';
COMMENT ON COLUMN links.weight IS 'Link strength (0-1) for visualization and path finding';
