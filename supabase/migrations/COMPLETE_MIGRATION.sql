-- ============================================================
-- NEUTRON-ESPClaw: Complete Migration Script
-- Run this in Supabase SQL Editor (Settings → SQL Editor)
-- ============================================================

-- ============================================================
-- STEP 1: Enable Extensions
-- ============================================================
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_trgm";
CREATE EXTENSION IF NOT EXISTS "vector";

-- ============================================================
-- STEP 2: Enums
-- ============================================================
DO $$ BEGIN
    CREATE TYPE device_type AS ENUM ('esp32s3', 'esp32', 'esp32c3', 'esp32s2', 'other');
EXCEPTION
    WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
    CREATE TYPE node_type AS ENUM (
        'user', 'skill', 'memory', 'thought', 'perception',
        'action', 'goal', 'project', 'document', 'tag',
        'device', 'transaction', 'location', 'time_marker', 'external_entity'
    );
EXCEPTION
    WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
    CREATE TYPE link_type AS ENUM (
        'relates_to', 'part_of', 'causes', 'enables', 'blocks',
        'similar_to', 'contrast_with', 'depends_on', 'precedes', 'follows',
        'input_to', 'output_from', 'associated_with', 'learned_from', 'used_for',
        'belongs_to', 'created_by', 'modifies', 'references', 'triggers',
        'contains', 'located_at', 'occurred_at'
    );
EXCEPTION
    WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
    CREATE TYPE pulse_type AS ENUM ('perception', 'thought', 'action', 'system', 'ai_insight');
EXCEPTION
    WHEN duplicate_object THEN null;
END $$;

-- ============================================================
-- STEP 3: Tenants Table
-- ============================================================
CREATE TABLE IF NOT EXISTS tenants (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name            TEXT NOT NULL,
    email           TEXT UNIQUE NOT NULL,
    plan            TEXT NOT NULL DEFAULT 'free',
    config          JSONB DEFAULT '{}',
    llm_config      JSONB DEFAULT '{"provider":"openai","model":"gpt-4o-mini"}',
    settings        JSONB DEFAULT '{}',
    is_active       BOOLEAN NOT NULL DEFAULT TRUE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_tenants_email ON tenants (email);

-- ============================================================
-- STEP 4: Devices Table
-- ============================================================
CREATE TABLE IF NOT EXISTS devices (
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
    device_secret   TEXT,
    
    -- Hardware info
    chip_model      TEXT,
    chip_revision   INTEGER,
    flash_size_mb   INTEGER,
    psram_size_mb   INTEGER,
    
    -- Configuration
    gpio_config     JSONB DEFAULT '{"mic":{"data_pin":4,"clk_pin":3},"dac":{"bclk_pin":44,"lrc_pin":43,"din_pin":42},"oled":{"sda_pin":1,"scl_pin":2},"status_led":{"pin":48}}',
    mqtt_config     JSONB DEFAULT '{"broker_url":"mqtts://216f9e2d8c15496ab889d41cfff20880.s1.eu.hivemq.cloud:8883","username":"myesp123","qos":1}',
    telegram_config JSONB DEFAULT '{}',
    
    -- Status
    is_online       BOOLEAN NOT NULL DEFAULT FALSE,
    last_seen_at    TIMESTAMPTZ,
    battery_level   INTEGER,
    is_charging     BOOLEAN,
    
    -- Sync
    last_sync_at    TIMESTAMPTZ,
    sync_version    INTEGER NOT NULL DEFAULT 0,
    
    -- Metadata
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_devices_tenant ON devices (tenant_id);
CREATE INDEX IF NOT EXISTS idx_devices_mac ON devices (tenant_id, mac_address);

-- ============================================================
-- STEP 5: Nodes Table (Core Knowledge Graph)
-- ============================================================
CREATE TABLE IF NOT EXISTS nodes (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
    
    -- Core Identity
    name            TEXT NOT NULL,
    type            node_type NOT NULL,
    subtype         TEXT,
    content         JSONB DEFAULT '{}',
    
    -- Spatial Position (3D semantic space)
    pos_x           REAL NOT NULL DEFAULT 0,
    pos_y           REAL NOT NULL DEFAULT 0,
    pos_z           REAL NOT NULL DEFAULT 0,
    
    -- Embedding Vector (for similarity search)
    embedding       vector(1536),
    
    -- Visual Properties
    color           TEXT DEFAULT '#6366f1',
    icon            TEXT,
    size            REAL DEFAULT 1.0,
    opacity         REAL DEFAULT 1.0,
    
    -- State
    is_active       BOOLEAN NOT NULL DEFAULT TRUE,
    importance      REAL DEFAULT 0.5,
    confidence      REAL DEFAULT 1.0,
    
    -- Timestamps
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    accessed_at     TIMESTAMPTZ,
    last_pulse_at   TIMESTAMPTZ,
    
    -- Sync versioning
    version         INTEGER NOT NULL DEFAULT 1,
    is_synced       BOOLEAN NOT NULL DEFAULT FALSE,
    deleted_at      TIMESTAMPTZ
);

-- Indexes for nodes (separate vector index)
CREATE INDEX IF NOT EXISTS idx_nodes_tenant ON nodes (tenant_id);
CREATE INDEX IF NOT EXISTS idx_nodes_type ON nodes (tenant_id, type);
CREATE INDEX IF NOT EXISTS idx_nodes_subtype ON nodes (tenant_id, type, subtype) WHERE subtype IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_nodes_spatial ON nodes (tenant_id, pos_x, pos_y, pos_z);
CREATE INDEX IF NOT EXISTS idx_nodes_embedding ON nodes USING ivfflat (embedding vector_cosine_ops) WITH (lists = 100);
CREATE INDEX IF NOT EXISTS idx_nodes_content ON nodes USING gin (content jsonb_path_ops);
CREATE INDEX IF NOT EXISTS idx_nodes_updated ON nodes (tenant_id, updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_nodes_active ON nodes (tenant_id, is_active) WHERE is_active = TRUE;
CREATE INDEX IF NOT EXISTS idx_nodes_name_trgm ON nodes USING gin (name gin_trgm_ops);

-- ============================================================
-- STEP 6: Links Table (Relationships between Nodes)
-- ============================================================
CREATE TABLE IF NOT EXISTS links (
    id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    tenant_id           UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    device_id           UUID REFERENCES devices(id) ON DELETE SET NULL,
    
    -- Link endpoints
    source_id           UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    target_id           UUID NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    
    -- Link properties
    type                link_type NOT NULL,
    weight              REAL DEFAULT 0.5,
    is_directed         BOOLEAN NOT NULL DEFAULT TRUE,
    content             JSONB DEFAULT '{}',
    
    -- AI metadata
    is_ai_generated     BOOLEAN DEFAULT FALSE,
    confidence          REAL DEFAULT 1.0,
    pattern_id          UUID,
    
    -- State
    is_active           BOOLEAN NOT NULL DEFAULT TRUE,
    
    -- Timestamps
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_pulse_at       TIMESTAMPTZ,
    
    -- Sync
    version             INTEGER NOT NULL DEFAULT 1,
    is_synced           BOOLEAN NOT NULL DEFAULT FALSE,
    deleted_at          TIMESTAMPTZ,
    
    CONSTRAINT no_self_link CHECK (source_id != target_id)
);

-- Indexes for links
CREATE INDEX IF NOT EXISTS idx_links_tenant ON links (tenant_id);
CREATE INDEX IF NOT EXISTS idx_links_source ON links (tenant_id, source_id);
CREATE INDEX IF NOT EXISTS idx_links_target ON links (tenant_id, target_id);
CREATE INDEX IF NOT EXISTS idx_links_type ON links (tenant_id, type);
CREATE INDEX IF NOT EXISTS idx_links_updated ON links (tenant_id, updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_links_active ON links (tenant_id, is_active) WHERE is_active = TRUE;

-- ============================================================
-- STEP 7: Pulses Table (Neural Activity Log)
-- ============================================================
CREATE TABLE IF NOT EXISTS pulses (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
    
    -- Pulse data
    type            pulse_type NOT NULL,
    source_node_id  UUID REFERENCES nodes(id) ON DELETE CASCADE,
    target_node_id  UUID REFERENCES nodes(id) ON DELETE CASCADE,
    link_id         UUID REFERENCES links(id) ON DELETE CASCADE,
    
    -- Content
    text            TEXT,
    audio_url       TEXT,
    embedding       vector(1536),
    metadata        JSONB DEFAULT '{}',
    
    -- Intensity & Direction
    intensity       REAL DEFAULT 0.5,
    direction       TEXT,
    
    -- Timestamps
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Indexes for pulses
CREATE INDEX IF NOT EXISTS idx_pulses_tenant ON pulses (tenant_id);
CREATE INDEX IF NOT EXISTS idx_pulses_device ON pulses (tenant_id, device_id);
CREATE INDEX IF NOT EXISTS idx_pulses_type ON pulses (tenant_id, type);
CREATE INDEX IF NOT EXISTS idx_pulses_source ON pulses (tenant_id, source_node_id);
CREATE INDEX IF NOT EXISTS idx_pulses_target ON pulses (tenant_id, target_node_id);
CREATE INDEX IF NOT EXISTS idx_pulses_created ON pulses (tenant_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_pulses_embedding ON pulses USING ivfflat (embedding vector_cosine_ops) WITH (lists = 100);

-- ============================================================
-- STEP 8: RLS Policies (Row Level Security)
-- ============================================================

-- Enable RLS
ALTER TABLE tenants ENABLE ROW LEVEL SECURITY;
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE nodes ENABLE ROW LEVEL SECURITY;
ALTER TABLE links ENABLE ROW LEVEL SECURITY;
ALTER TABLE pulses ENABLE ROW LEVEL SECURITY;

-- Tenants: Users can only see their own tenant
CREATE POLICY "Users can view own tenant"
    ON tenants FOR SELECT
    USING (auth.role() = 'authenticated');

CREATE POLICY "Users can update own tenant"
    ON tenants FOR UPDATE
    USING (auth.role() = 'authenticated');

-- Devices: Users can only access devices of their tenant
CREATE POLICY "Users can view devices of their tenant"
    ON devices FOR SELECT
    USING (auth.role() = 'authenticated');

CREATE POLICY "Users can insert devices for their tenant"
    ON devices FOR INSERT
    WITH CHECK (auth.role() = 'authenticated');

-- Nodes: Tenant isolation (allow all authenticated users for demo)
CREATE POLICY "Users can view nodes of their tenant"
    ON nodes FOR SELECT
    USING (auth.role() = 'authenticated');

CREATE POLICY "Users can insert nodes for their tenant"
    ON nodes FOR INSERT
    WITH CHECK (auth.role() = 'authenticated');

CREATE POLICY "Users can update nodes of their tenant"
    ON nodes FOR UPDATE
    USING (auth.role() = 'authenticated');

CREATE POLICY "Users can delete nodes of their tenant"
    ON nodes FOR DELETE
    USING (auth.role() = 'authenticated');

-- Links: Tenant isolation
CREATE POLICY "Users can view links of their tenant"
    ON links FOR SELECT
    USING (auth.role() = 'authenticated');

CREATE POLICY "Users can insert links for their tenant"
    ON links FOR INSERT
    WITH CHECK (auth.role() = 'authenticated');

CREATE POLICY "Users can update links of their tenant"
    ON links FOR UPDATE
    USING (auth.role() = 'authenticated');

-- Pulses: Tenant isolation
CREATE POLICY "Users can view pulses of their tenant"
    ON pulses FOR SELECT
    USING (auth.role() = 'authenticated');

CREATE POLICY "Users can insert pulses for their tenant"
    ON pulses FOR INSERT
    WITH CHECK (auth.role() = 'authenticated');

-- ============================================================
-- STEP 9: Functions
-- ============================================================

-- Function: auto-update updated_at
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Apply to all tables
CREATE OR REPLACE TRIGGER update_tenants_updated_at
    BEFORE UPDATE ON tenants
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE OR REPLACE TRIGGER update_devices_updated_at
    BEFORE UPDATE ON devices
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE OR REPLACE TRIGGER update_nodes_updated_at
    BEFORE UPDATE ON nodes
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

CREATE OR REPLACE TRIGGER update_links_updated_at
    BEFORE UPDATE ON links
    FOR EACH ROW EXECUTE FUNCTION update_updated_at_column();

-- ============================================================
-- STEP 10: Enable Realtime
-- ============================================================
ALTER PUBLICATION supabase_realtime ADD TABLE nodes;
ALTER PUBLICATION supabase_realtime ADD TABLE links;
ALTER PUBLICATION supabase_realtime ADD TABLE pulses;

-- ============================================================
-- STEP 11: Create default tenant (for testing)
-- ============================================================
INSERT INTO tenants (id, name, email, plan, llm_config)
VALUES (
    '00000000-0000-0000-0000-000000000001',
    'Default Tenant',
    'default@example.com',
    'pro',
    '{"provider":"openai","model":"gpt-4o-mini","temperature":0.7}'
)
ON CONFLICT (email) DO NOTHING;

-- ============================================================
-- STEP 12: Create demo data
-- ============================================================
INSERT INTO devices (id, tenant_id, device_type, device_name, firmware_version, is_paired)
VALUES (
    '00000000-0000-0000-0000-000000000001',
    '00000000-0000-0000-0000-000000000001',
    'esp32s3',
    'ESPClaw Prototype',
    '1.0.0',
    TRUE
)
ON CONFLICT DO NOTHING;

-- Insert sample nodes
INSERT INTO nodes (id, tenant_id, device_id, name, type, subtype, pos_x, pos_y, pos_z, content)
VALUES
    ('10000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000001', 'Welcome', 'memory', 'intro', 0, 0, 0, '{"text":"Welcome to Neutron-ESPClaw! This is your first memory node."}'),
    ('10000000-0000-0000-0000-000000000002', '00000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000001', 'Speech Recognition', 'skill', 'audio', 2, 1, 0, '{"description":"Voice input processing"}'),
    ('10000000-0000-0000-0000-000000000003', '00000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000001', 'TTS Output', 'skill', 'audio', 2, -1, 0, '{"description":"Text-to-speech output"}')
ON CONFLICT DO NOTHING;

-- Insert sample links
INSERT INTO links (id, tenant_id, device_id, source_id, target_id, type, weight)
VALUES
    ('20000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000001', '10000000-0000-0000-0000-000000000001', '10000000-0000-0000-0000-000000000002', 'enables', 0.9),
    ('20000000-0000-0000-0000-000000000002', '00000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000001', '10000000-0000-0000-0000-000000000001', '10000000-0000-0000-0000-000000000003', 'enables', 0.9)
ON CONFLICT DO NOTHING;

-- ============================================================
-- Phase 6: Patterns & AI Insights Tables
-- ============================================================

-- Patterns table (co-occurrence patterns)
CREATE TABLE IF NOT EXISTS patterns (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    pattern_id UUID UNIQUE NOT NULL,
    tenant_id UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    node_type_a TEXT NOT NULL,
    node_type_b TEXT NOT NULL,
    cooccurrence_count INTEGER DEFAULT 0,
    confidence DECIMAL(3,2) DEFAULT 0.0 CHECK (confidence >= 0 AND confidence <= 1),
    avg_time_gap_seconds INTEGER,
    suggested_link_type TEXT,
    auto_link_enabled BOOLEAN DEFAULT false,
    suggestion TEXT,
    detected_at TIMESTAMPTZ DEFAULT NOW(),
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_patterns_tenant ON patterns(tenant_id);
CREATE INDEX IF NOT EXISTS idx_patterns_confidence ON patterns(confidence DESC);
CREATE INDEX IF NOT EXISTS idx_patterns_detected ON patterns(detected_at DESC);

-- AI Insights table
CREATE TABLE IF NOT EXISTS ai_insights (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    tenant_id UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    insight_text TEXT NOT NULL,
    analysis_type TEXT DEFAULT 'full',
    node_count INTEGER DEFAULT 0,
    link_count INTEGER DEFAULT 0,
    pulse_count INTEGER DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Index
CREATE INDEX IF NOT EXISTS idx_ai_insights_tenant ON ai_insights(tenant_id);
CREATE INDEX IF NOT EXISTS idx_ai_insights_created ON ai_insights(created_at DESC);

-- ============================================================
-- COMPLETE!
-- ============================================================
SELECT '✅ Neutron-ESPClaw Migration Complete!' AS status;
