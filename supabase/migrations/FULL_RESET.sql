-- ============================================================
-- ESPClaw: COMPLETE DATABASE RESET & MIGRATION
-- Run this in: Supabase Dashboard → SQL Editor
-- WARNING: This DROPs ALL tables and recreates from scratch.
-- ============================================================

-- ============================================================
-- STEP 1: RESET (drop everything)
-- ============================================================
DROP SCHEMA IF EXISTS public CASCADE;
CREATE SCHEMA public;
GRANT ALL ON SCHEMA public TO postgres;
GRANT ALL ON SCHEMA public TO public;

-- ============================================================
-- STEP 2: Enable Extensions
-- ============================================================
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_trgm";
CREATE EXTENSION IF NOT EXISTS "vector";

-- ============================================================
-- STEP 3: Enums
-- ============================================================

DO $$ BEGIN
  CREATE TYPE device_type AS ENUM ('esp32s3', 'esp32', 'esp32c3', 'esp32s2', 'other');
EXCEPTION WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
  CREATE TYPE node_type AS ENUM (
    'user', 'device', 'skill', 'memory', 'tag',
    'transaction', 'entity', 'event', 'goal',
    'routine', 'insight', 'pulse', 'webhook',
    'session', 'context'
  );
EXCEPTION WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
  CREATE TYPE link_type AS ENUM (
    'owns', 'part_of', 'member_of', 'related_to', 'similar_to',
    'caused_by', 'resulted_in', 'happened_before', 'happened_after',
    'triggered', 'followed_by', 'preceded_by', 'associated_with',
    'enabled_by', 'blocks', 'depends_on', 'extends', 'implements'
  );
EXCEPTION WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
  CREATE TYPE pulse_type AS ENUM (
    'perception', 'thought', 'action', 'system', 'ai_insight'
  );
EXCEPTION WHEN duplicate_object THEN null;
END $$;

-- ============================================================
-- STEP 4: Tables
-- ============================================================

-- Tenants
CREATE TABLE IF NOT EXISTS tenants (
  id            UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  name          TEXT NOT NULL,
  slug          TEXT UNIQUE,
  plan          TEXT DEFAULT 'free',
  settings      JSONB DEFAULT '{}',
  created_at    TIMESTAMPTZ DEFAULT NOW(),
  updated_at    TIMESTAMPTZ DEFAULT NOW()
);

-- Devices
CREATE TABLE IF NOT EXISTS devices (
  id                UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id         UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  device_name       TEXT,
  device_type       device_type DEFAULT 'esp32s3',
  mac_address       TEXT UNIQUE,
  is_paired         BOOLEAN DEFAULT FALSE,
  pair_code         TEXT,
  pair_expires_at   TIMESTAMPTZ,
  last_seen_at      TIMESTAMPTZ,
  is_online         BOOLEAN DEFAULT FALSE,
  firmware_version   TEXT,
  metadata          JSONB DEFAULT '{}',
  created_at        TIMESTAMPTZ DEFAULT NOW(),
  updated_at        TIMESTAMPTZ DEFAULT NOW()
);

-- Nodes
CREATE TABLE IF NOT EXISTS nodes (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
  name            TEXT NOT NULL,
  type            node_type,
  subtype         TEXT,
  content         JSONB DEFAULT '{}',
  pos_x           REAL DEFAULT 0,
  pos_y           REAL DEFAULT 0,
  pos_z           REAL DEFAULT 0,
  color           TEXT,
  icon            TEXT,
  size            REAL DEFAULT 1,
  importance      REAL DEFAULT 0.5,
  embedding       VECTOR(1536),
  is_active       BOOLEAN DEFAULT TRUE,
  is_synced       BOOLEAN DEFAULT FALSE,
  is_ai_generated BOOLEAN DEFAULT FALSE,
  last_pulse_at   TIMESTAMPTZ,
  deleted_at      TIMESTAMPTZ,
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- Links
CREATE TABLE IF NOT EXISTS links (
  id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id           UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  device_id           UUID REFERENCES devices(id) ON DELETE SET NULL,
  source_node_id      UUID REFERENCES nodes(id) ON DELETE CASCADE,
  target_node_id      UUID REFERENCES nodes(id) ON DELETE CASCADE,
  type               link_type DEFAULT 'related_to',
  weight              REAL DEFAULT 0.5,
  confidence          REAL,
  content             JSONB DEFAULT '{}',
  is_ai_generated     BOOLEAN DEFAULT FALSE,
  is_synced           BOOLEAN DEFAULT FALSE,
  pattern_id          UUID,
  is_active           BOOLEAN DEFAULT TRUE,
  deleted_at          TIMESTAMPTZ,
  created_at          TIMESTAMPTZ DEFAULT NOW(),
  updated_at          TIMESTAMPTZ DEFAULT NOW(),
  UNIQUE(tenant_id, source_node_id, target_node_id)
);

-- Pulses
CREATE TABLE IF NOT EXISTS pulses (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
  source_node_id  UUID REFERENCES nodes(id) ON DELETE SET NULL,
  target_node_id  UUID REFERENCES nodes(id) ON DELETE SET NULL,
  link_id         UUID REFERENCES links(id) ON DELETE SET NULL,
  type            pulse_type DEFAULT 'action',
  text            TEXT,
  intensity       REAL DEFAULT 0.5,
  metadata        JSONB DEFAULT '{}',
  created_at      TIMESTAMPTZ DEFAULT NOW()
);

-- Patterns
CREATE TABLE IF NOT EXISTS patterns (
  id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id           UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  node_type_a         TEXT NOT NULL,
  node_type_b         TEXT NOT NULL,
  suggested_link_type TEXT DEFAULT 'related_to',
  confidence          REAL DEFAULT 0.5,
  occurrence_count    INTEGER DEFAULT 1,
  last_seen_at        TIMESTAMPTZ DEFAULT NOW(),
  is_active           BOOLEAN DEFAULT TRUE,
  auto_link_enabled   BOOLEAN DEFAULT FALSE,
  created_at          TIMESTAMPTZ DEFAULT NOW(),
  updated_at          TIMESTAMPTZ DEFAULT NOW()
);

-- AI Insights
CREATE TABLE IF NOT EXISTS ai_insights (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  node_count      INTEGER,
  link_count      INTEGER,
  pulse_count     INTEGER,
  insight_text    TEXT NOT NULL,
  analysis_type   TEXT DEFAULT 'full',
  created_at      TIMESTAMPTZ DEFAULT NOW()
);

-- Sessions (for ReAct agent)
CREATE TABLE IF NOT EXISTS sessions (
  id                UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id         UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  device_id         UUID REFERENCES devices(id) ON DELETE CASCADE,
  session_key       TEXT UNIQUE NOT NULL,
  session_data      JSONB DEFAULT '{}',
  is_active         BOOLEAN DEFAULT TRUE,
  last_active_at    TIMESTAMPTZ DEFAULT NOW(),
  created_at        TIMESTAMPTZ DEFAULT NOW(),
  updated_at        TIMESTAMPTZ DEFAULT NOW()
);

-- Graph Stats
CREATE TABLE IF NOT EXISTS graph_stats (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  date            DATE NOT NULL,
  node_count      INTEGER DEFAULT 0,
  link_count      INTEGER DEFAULT 0,
  pulse_count     INTEGER DEFAULT 0,
  unique_types    TEXT[],
  created_at      TIMESTAMPTZ DEFAULT NOW(),
  updated_at      TIMESTAMPTZ DEFAULT NOW(),
  UNIQUE(tenant_id, date)
);

-- Sync Log
CREATE TABLE IF NOT EXISTS sync_log (
  id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
  device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
  sync_type       TEXT,
  status          TEXT DEFAULT 'pending',
  nodes_synced    INTEGER DEFAULT 0,
  links_synced    INTEGER DEFAULT 0,
  pulses_synced   INTEGER DEFAULT 0,
  error_message   TEXT,
  duration_ms     INTEGER,
  created_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================
-- STEP 5: Indexes
-- ============================================================

-- Devices
CREATE INDEX idx_devices_tenant_id ON devices(tenant_id);
CREATE INDEX idx_devices_mac_address ON devices(mac_address);
CREATE INDEX idx_devices_pair_code ON devices(pair_code) WHERE pair_code IS NOT NULL;

-- Nodes
CREATE INDEX idx_nodes_tenant_id ON nodes(tenant_id);
CREATE INDEX idx_nodes_device_id ON nodes(device_id);
CREATE INDEX idx_nodes_type ON nodes(tenant_id, type);
CREATE INDEX idx_nodes_name_trgm ON nodes USING gin(name gin_trgm_ops);
CREATE INDEX idx_nodes_embedding ON nodes USING ivfflat(embedding vector_cosine_ops) WITH (lists = 100);
CREATE INDEX idx_nodes_active ON nodes(tenant_id, is_active) WHERE is_active = TRUE;
CREATE INDEX idx_nodes_last_pulse ON nodes(tenant_id, last_pulse_at DESC) WHERE last_pulse_at IS NOT NULL;

-- Links
CREATE INDEX idx_links_tenant_id ON links(tenant_id);
CREATE INDEX idx_links_source ON links(source_node_id);
CREATE INDEX idx_links_target ON links(target_node_id);
CREATE INDEX idx_links_type ON links(tenant_id, type);
CREATE INDEX idx_links_active ON links(tenant_id, is_active) WHERE is_active = TRUE;

-- Pulses
CREATE INDEX idx_pulses_tenant_id ON pulses(tenant_id);
CREATE INDEX idx_pulses_type ON pulses(tenant_id, type);
CREATE INDEX idx_pulses_created ON pulses(tenant_id, created_at DESC);
CREATE INDEX idx_pulses_source ON pulses(source_node_id);
CREATE INDEX idx_pulses_target ON pulses(target_node_id);

-- Patterns
CREATE INDEX idx_patterns_tenant ON patterns(tenant_id, is_active);
CREATE INDEX idx_patterns_confidence ON patterns(tenant_id, confidence DESC) WHERE is_active = TRUE;

-- AI Insights
CREATE INDEX idx_insights_tenant ON ai_insights(tenant_id, created_at DESC);

-- Sessions
CREATE INDEX idx_sessions_tenant ON sessions(tenant_id, is_active);
CREATE INDEX idx_sessions_key ON sessions(session_key);

-- Graph Stats
CREATE INDEX idx_graph_stats_tenant_date ON graph_stats(tenant_id, date DESC);

-- Sync Log
CREATE INDEX idx_sync_log_tenant ON sync_log(tenant_id, created_at DESC);

-- ============================================================
-- STEP 6: Auto-update updated_at trigger
-- ============================================================

CREATE OR REPLACE FUNCTION public.auto_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER auto_updated_at_tenants
  BEFORE UPDATE ON tenants FOR EACH ROW EXECUTE FUNCTION public.auto_updated_at();

CREATE TRIGGER auto_updated_at_devices
  BEFORE UPDATE ON devices FOR EACH ROW EXECUTE FUNCTION public.auto_updated_at();

CREATE TRIGGER auto_updated_at_nodes
  BEFORE UPDATE ON nodes FOR EACH ROW EXECUTE FUNCTION public.auto_updated_at();

CREATE TRIGGER auto_updated_at_links
  BEFORE UPDATE ON links FOR EACH ROW EXECUTE FUNCTION public.auto_updated_at();

CREATE TRIGGER auto_updated_at_sessions
  BEFORE UPDATE ON sessions FOR EACH ROW EXECUTE FUNCTION public.auto_updated_at();

CREATE TRIGGER auto_updated_at_graph_stats
  BEFORE UPDATE ON graph_stats FOR EACH ROW EXECUTE FUNCTION public.auto_updated_at();

-- ============================================================
-- STEP 7: RLS — Enable + Tenant Isolation Policies
-- ============================================================

ALTER TABLE tenants        ENABLE ROW LEVEL SECURITY;
ALTER TABLE devices        ENABLE ROW LEVEL SECURITY;
ALTER TABLE nodes          ENABLE ROW LEVEL SECURITY;
ALTER TABLE links          ENABLE ROW LEVEL SECURITY;
ALTER TABLE pulses         ENABLE ROW LEVEL SECURITY;
ALTER TABLE patterns       ENABLE ROW LEVEL SECURITY;
ALTER TABLE ai_insights    ENABLE ROW LEVEL SECURITY;
ALTER TABLE sessions       ENABLE ROW LEVEL SECURITY;
ALTER TABLE graph_stats    ENABLE ROW LEVEL SECURITY;
ALTER TABLE sync_log       ENABLE ROW LEVEL SECURITY;

-- Helper: get tenant_id from auth user's raw_user_meta_data
-- Users must have tenant_id in their metadata: { "tenant_id": "uuid" }
CREATE OR REPLACE FUNCTION public.get_user_tenant_id()
RETURNS UUID
LANGUAGE sql
STABLE
SECURITY DEFINER
SET search_path = public
AS $$
  SELECT (raw_user_meta_data->>'tenant_id')::UUID
  FROM auth.users
  WHERE id = auth.uid()
  LIMIT 1;
$$;

-- Helper: get tenant_id for service role (bypass via direct tenant_id)
CREATE OR REPLACE FUNCTION public.get_service_tenant_id(request_tenant_id UUID)
RETURNS UUID
LANGUAGE sql
STABLE
SECURITY DEFINER
SET search_path = public
AS $$
  SELECT request_tenant_id;
$$;

-- ============================================================
-- Tenants policies
-- ============================================================
CREATE POLICY "tenants_select_own"
  ON tenants FOR SELECT
  USING (id = public.get_user_tenant_id());

CREATE POLICY "tenants_update_own"
  ON tenants FOR UPDATE
  USING (id = public.get_user_tenant_id())
  WITH CHECK (id = public.get_user_tenant_id());

-- ============================================================
-- Devices policies
-- ============================================================
CREATE POLICY "devices_select_own_tenant"
  ON devices FOR SELECT
  USING (tenant_id = public.get_user_tenant_id() OR tenant_id = public.get_service_tenant_id(NULL));

CREATE POLICY "devices_insert_own_tenant"
  ON devices FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "devices_update_own_tenant"
  ON devices FOR UPDATE
  USING (tenant_id = public.get_user_tenant_id())
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "devices_delete_own_tenant"
  ON devices FOR DELETE
  USING (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- Nodes policies
-- ============================================================
CREATE POLICY "nodes_select_own_tenant"
  ON nodes FOR SELECT
  USING (tenant_id = public.get_user_tenant_id() OR tenant_id = public.get_service_tenant_id(NULL));

CREATE POLICY "nodes_insert_own_tenant"
  ON nodes FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "nodes_update_own_tenant"
  ON nodes FOR UPDATE
  USING (tenant_id = public.get_user_tenant_id())
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "nodes_delete_own_tenant"
  ON nodes FOR DELETE
  USING (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- Links policies
-- ============================================================
CREATE POLICY "links_select_own_tenant"
  ON links FOR SELECT
  USING (tenant_id = public.get_user_tenant_id() OR tenant_id = public.get_service_tenant_id(NULL));

CREATE POLICY "links_insert_own_tenant"
  ON links FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "links_update_own_tenant"
  ON links FOR UPDATE
  USING (tenant_id = public.get_user_tenant_id())
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "links_delete_own_tenant"
  ON links FOR DELETE
  USING (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- Pulses policies
-- ============================================================
CREATE POLICY "pulses_select_own_tenant"
  ON pulses FOR SELECT
  USING (tenant_id = public.get_user_tenant_id() OR tenant_id = public.get_service_tenant_id(NULL));

CREATE POLICY "pulses_insert_own_tenant"
  ON pulses FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "pulses_update_own_tenant"
  ON pulses FOR UPDATE
  USING (tenant_id = public.get_user_tenant_id())
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "pulses_delete_own_tenant"
  ON pulses FOR DELETE
  USING (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- Patterns policies
-- ============================================================
CREATE POLICY "patterns_select_own_tenant"
  ON patterns FOR SELECT
  USING (tenant_id = public.get_user_tenant_id() OR tenant_id = public.get_service_tenant_id(NULL));

CREATE POLICY "patterns_insert_own_tenant"
  ON patterns FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "patterns_update_own_tenant"
  ON patterns FOR UPDATE
  USING (tenant_id = public.get_user_tenant_id())
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "patterns_delete_own_tenant"
  ON patterns FOR DELETE
  USING (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- AI Insights policies
-- ============================================================
CREATE POLICY "ai_insights_select_own_tenant"
  ON ai_insights FOR SELECT
  USING (tenant_id = public.get_user_tenant_id() OR tenant_id = public.get_service_tenant_id(NULL));

CREATE POLICY "ai_insights_insert_own_tenant"
  ON ai_insights FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- Sessions policies
-- ============================================================
CREATE POLICY "sessions_select_own_tenant"
  ON sessions FOR SELECT
  USING (tenant_id = public.get_user_tenant_id());

CREATE POLICY "sessions_insert_own_tenant"
  ON sessions FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "sessions_update_own_tenant"
  ON sessions FOR UPDATE
  USING (tenant_id = public.get_user_tenant_id())
  WITH CHECK (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- Graph Stats policies
-- ============================================================
CREATE POLICY "graph_stats_select_own_tenant"
  ON graph_stats FOR SELECT
  USING (tenant_id = public.get_user_tenant_id());

CREATE POLICY "graph_stats_insert_own_tenant"
  ON graph_stats FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

CREATE POLICY "graph_stats_update_own_tenant"
  ON graph_stats FOR UPDATE
  USING (tenant_id = public.get_user_tenant_id())
  WITH CHECK (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- Sync Log policies
-- ============================================================
CREATE POLICY "sync_log_select_own_tenant"
  ON sync_log FOR SELECT
  USING (tenant_id = public.get_user_tenant_id());

CREATE POLICY "sync_log_insert_own_tenant"
  ON sync_log FOR INSERT
  WITH CHECK (tenant_id = public.get_user_tenant_id());

-- ============================================================
-- STEP 8: Useful Database Functions
-- ============================================================

-- Update graph stats for a tenant
CREATE OR REPLACE FUNCTION public.update_graph_stats(p_tenant_id UUID)
RETURNS VOID AS $$
BEGIN
  INSERT INTO graph_stats (tenant_id, date, node_count, link_count, pulse_count, unique_types)
  VALUES (
    p_tenant_id,
    CURRENT_DATE,
    (SELECT COUNT(*) FROM nodes WHERE tenant_id = p_tenant_id AND is_active = TRUE),
    (SELECT COUNT(*) FROM links WHERE tenant_id = p_tenant_id AND is_active = TRUE),
    (SELECT COUNT(*) FROM pulses WHERE tenant_id = p_tenant_id AND created_at > NOW() - INTERVAL '1 day'),
    ARRAY(SELECT DISTINCT type::TEXT FROM nodes WHERE tenant_id = p_tenant_id AND is_active = TRUE)
  )
  ON CONFLICT (tenant_id, date) DO UPDATE
  SET
    node_count = EXCLUDED.node_count,
    link_count = EXCLUDED.link_count,
    pulse_count = EXCLUDED.pulse_count,
    unique_types = EXCLUDED.unique_types,
    updated_at = NOW();
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- ============================================================
-- STEP 9: Grant permissions
-- ============================================================
GRANT USAGE ON SCHEMA public TO authenticated, anon, service_role;
GRANT ALL ON ALL TABLES IN SCHEMA public TO authenticated, anon, service_role;
GRANT ALL ON ALL SEQUENCES IN SCHEMA public TO authenticated, anon, service_role;
GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA public TO authenticated, anon, service_role;

-- ============================================================
-- STEP 10: Seed default tenant (for development)
-- ============================================================
INSERT INTO tenants (id, name, slug, plan)
VALUES (
  '00000000-0000-0000-0000-000000000001',
  'ESPClaw Dev',
  'espclaw-dev',
  'pro'
) ON CONFLICT (slug) DO NOTHING;

-- ============================================================
-- COMPLETE!
-- ============================================================
SELECT '✅ Database reset complete! Tables: ' || COUNT(*) || ', RLS policies: ' ||
  (SELECT COUNT(*) FROM pg_policies WHERE schemaname = 'public')
FROM information_schema.tables WHERE table_schema = 'public' AND table_type = 'BASE TABLE';
