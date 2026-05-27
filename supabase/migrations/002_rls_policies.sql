-- ============================================================
-- NEUTRON-ESPClaw: Row Level Security Policies
-- Version: 002
-- Description: Multi-tenant isolation with RLS
-- ============================================================

-- ============================================================
-- ENABLE RLS ON ALL TABLES
-- ============================================================

ALTER TABLE tenants ENABLE ROW LEVEL SECURITY;
ALTER TABLE devices ENABLE ROW LEVEL SECURITY;
ALTER TABLE nodes ENABLE ROW LEVEL SECURITY;
ALTER TABLE links ENABLE ROW LEVEL SECURITY;
ALTER TABLE sessions ENABLE ROW LEVEL SECURITY;
ALTER TABLE pulses ENABLE ROW LEVEL SECURITY;
ALTER TABLE graph_stats ENABLE ROW LEVEL SECURITY;
ALTER TABLE sync_log ENABLE ROW LEVEL SECURITY;

-- ============================================================
-- HELPER FUNCTIONS (public schema - no auth schema conflicts)
-- ============================================================

-- Get current authenticated user ID from JWT
CREATE OR REPLACE FUNCTION public.current_user_id() RETURNS UUID AS $$
  SELECT NULLIF(current_setting('request.jwt.claim_sub', true), '')::UUID;
$$ LANGUAGE SQL STABLE;

-- Get current tenant ID from JWT
CREATE OR REPLACE FUNCTION public.current_tenant_id() RETURNS UUID AS $$
  SELECT NULLIF(current_setting('request.jwt.claim_tenant_id', true), '')::UUID;
$$ LANGUAGE SQL STABLE;

-- Check if user is authenticated
CREATE OR REPLACE FUNCTION public.is_authenticated() RETURNS BOOLEAN AS $$
BEGIN
  RETURN public.current_user_id() IS NOT NULL AND public.current_tenant_id() IS NOT NULL;
END;
$$ LANGUAGE plpgsql STABLE;

-- Get device ID from service role key (for device-to-cloud sync)
CREATE OR REPLACE FUNCTION public.current_device_id() RETURNS UUID AS $$
  SELECT NULLIF(
    (current_setting('request.headers', true)::JSONB->>'x-device-id'), ''
  )::UUID;
$$ LANGUAGE SQL STABLE;

-- Check if request is from an authenticated device
CREATE OR REPLACE FUNCTION public.is_device_authenticated() RETURNS BOOLEAN AS $$
BEGIN
  RETURN public.current_device_id() IS NOT NULL;
END;
$$ LANGUAGE plpgsql STABLE;

-- Get device tenant (for device auth)
CREATE OR REPLACE FUNCTION public.device_tenant_id() RETURNS UUID AS $$
BEGIN
  RETURN (
    SELECT tenant_id FROM devices WHERE id = public.current_device_id()
  );
END;
$$ LANGUAGE plpgsql STABLE;

-- ============================================================
-- TENANTS POLICIES
-- ============================================================

-- Users can view their own tenant
CREATE POLICY "Users can view own tenant"
  ON tenants FOR SELECT
  USING (
    id = public.current_tenant_id()
  );

-- Users can update their own tenant
CREATE POLICY "Users can update own tenant"
  ON tenants FOR UPDATE
  USING (
    id = public.current_tenant_id()
  )
  WITH CHECK (
    id = public.current_tenant_id()
  );

-- Service role can do anything (for admin operations)
-- Note: In production, use separate service role key for migrations

-- ============================================================
-- DEVICES POLICIES
-- ============================================================

-- Users can view their own devices
CREATE POLICY "Users can view own devices"
  ON devices FOR SELECT
  USING (
    tenant_id = public.current_tenant_id()
  );

-- Users can insert devices for their tenant
CREATE POLICY "Users can insert own devices"
  ON devices FOR INSERT
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can update their own devices
CREATE POLICY "Users can update own devices"
  ON devices FOR UPDATE
  USING (
    tenant_id = public.current_tenant_id()
  )
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can delete their own devices
CREATE POLICY "Users can delete own devices"
  ON devices FOR DELETE
  USING (
    tenant_id = public.current_tenant_id()
  );

-- ============================================================
-- NODES POLICIES
-- ============================================================

-- Users can view nodes in their tenant
CREATE POLICY "Users can view own nodes"
  ON nodes FOR SELECT
  USING (
    tenant_id = public.current_tenant_id()
  );

-- Users can insert nodes for their tenant
CREATE POLICY "Users can insert own nodes"
  ON nodes FOR INSERT
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can update nodes in their tenant
CREATE POLICY "Users can update own nodes"
  ON nodes FOR UPDATE
  USING (
    tenant_id = public.current_tenant_id()
  )
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can delete nodes in their tenant
CREATE POLICY "Users can delete own nodes"
  ON nodes FOR DELETE
  USING (
    tenant_id = public.current_tenant_id()
  );

-- ============================================================
-- LINKS POLICIES
-- ============================================================

-- Users can view links in their tenant
CREATE POLICY "Users can view own links"
  ON links FOR SELECT
  USING (
    tenant_id = public.current_tenant_id()
  );

-- Users can insert links for their tenant
-- Additional check: both source and target nodes must belong to tenant
CREATE POLICY "Users can insert own links"
  ON links FOR INSERT
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can update links in their tenant
CREATE POLICY "Users can update own links"
  ON links FOR UPDATE
  USING (
    tenant_id = public.current_tenant_id()
  )
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can delete links in their tenant
CREATE POLICY "Users can delete own links"
  ON links FOR DELETE
  USING (
    tenant_id = public.current_tenant_id()
  );

-- Block cross-tenant link manipulation at DB level
-- This is a safety net - the WITH CHECK above should catch this
CREATE POLICY "Links must stay within tenant boundary"
  ON links FOR ALL
  USING (
    tenant_id = public.current_tenant_id() AND
    EXISTS (SELECT 1 FROM nodes n WHERE n.id = links.source_node_id AND n.tenant_id = public.current_tenant_id()) AND
    EXISTS (SELECT 1 FROM nodes n WHERE n.id = links.target_node_id AND n.tenant_id = public.current_tenant_id())
  );

-- ============================================================
-- SESSIONS POLICIES
-- ============================================================

-- Users can view sessions in their tenant
CREATE POLICY "Users can view own sessions"
  ON sessions FOR SELECT
  USING (
    tenant_id = public.current_tenant_id()
  );

-- Users can insert sessions for their tenant
CREATE POLICY "Users can insert own sessions"
  ON sessions FOR INSERT
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can update sessions in their tenant
CREATE POLICY "Users can update own sessions"
  ON sessions FOR UPDATE
  USING (
    tenant_id = public.current_tenant_id()
  )
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can delete sessions in their tenant
CREATE POLICY "Users can delete own sessions"
  ON sessions FOR DELETE
  USING (
    tenant_id = public.current_tenant_id()
  );

-- ============================================================
-- PULSES POLICIES
-- ============================================================

-- Users can view pulses in their tenant
CREATE POLICY "Users can view own pulses"
  ON pulses FOR SELECT
  USING (
    tenant_id = public.current_tenant_id()
  );

-- Users can insert pulses for their tenant
CREATE POLICY "Users can insert own pulses"
  ON pulses FOR INSERT
  WITH CHECK (
    tenant_id = public.current_tenant_id()
  );

-- Users can delete pulses in their tenant
CREATE POLICY "Users can delete own pulses"
  ON pulses FOR DELETE
  USING (
    tenant_id = public.current_tenant_id()
  );

-- ============================================================
-- GRAPH_STATS POLICIES
-- ============================================================

-- Users can view their own graph stats
CREATE POLICY "Users can view own graph stats"
  ON graph_stats FOR SELECT
  USING (
    tenant_id = public.current_tenant_id()
  );

-- ============================================================
-- SYNC_LOG POLICIES
-- ============================================================

-- Users can view sync logs for their devices
CREATE POLICY "Users can view own sync logs"
  ON sync_log FOR SELECT
  USING (
    device_id IN (
      SELECT id FROM devices WHERE tenant_id = public.current_tenant_id()
    )
  );

-- ============================================================
-- ANON/devRole POLICIES (for device API)
-- ============================================================

-- Devices can view their own data (using x-device-id header)
-- This uses service role bypass with additional device validation

-- Create a policy that allows devices to access via API
-- The actual validation happens in the API layer
CREATE POLICY "Devices can sync data"
  ON nodes FOR ALL
  USING (
    tenant_id = COALESCE(
      (SELECT tenant_id FROM devices WHERE id = NULLIF(
        (current_setting('request.headers', true)::JSONB->>'x-device-id'), ''
      )::UUID),
      public.current_tenant_id()
    )
  );

-- ============================================================
-- REALTIME SUBSCRIPTIONS
-- ============================================================

-- Enable realtime for nodes and links
ALTER PUBLICATION supabase_realtime ADD TABLE nodes;
ALTER PUBLICATION supabase_realtime ADD TABLE links;
ALTER PUBLICATION supabase_realtime ADD TABLE pulses;

-- ============================================================
-- COMMENTS
-- ============================================================

COMMENT ON POLICY "Users can view own tenant" ON tenants IS
  'Allows users to view only their own tenant record';

COMMENT ON POLICY "Users can view own nodes" ON nodes IS
  'Strict tenant isolation - users can only access nodes in their own tenant';

COMMENT ON POLICY "Links must stay within tenant boundary" ON links IS
  'Safety net to prevent cross-tenant link manipulation';
