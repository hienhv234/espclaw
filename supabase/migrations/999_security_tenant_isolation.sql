-- ============================================================
-- SECURITY MIGRATION: Tenant-Based RLS Policies
-- Run this AFTER COMPLETE_MIGRATION.sql
-- ============================================================
-- This migration replaces the permissive RLS policies with
-- proper tenant-based isolation.
--
-- IMPORTANT: Requires auth.users to have tenant_id in raw_user_meta_data
-- Add tenant_id to user metadata in Supabase Dashboard:
--   Settings → Authentication → Users → Select user → Metadata
--   Add: { "tenant_id": "uuid-of-tenant" }
-- ============================================================

-- ============================================================
-- STEP 1: Drop existing permissive policies
-- ============================================================

-- Tenants
DROP POLICY IF EXISTS "Users can view own tenant" ON tenants;
DROP POLICY IF EXISTS "Users can update own tenant" ON tenants;

-- Devices
DROP POLICY IF EXISTS "Users can view devices of their tenant" ON devices;
DROP POLICY IF EXISTS "Users can insert devices for their tenant" ON devices;

-- Nodes
DROP POLICY IF EXISTS "Users can view nodes of their tenant" ON nodes;
DROP POLICY IF EXISTS "Users can insert nodes for their tenant" ON nodes;
DROP POLICY IF EXISTS "Users can update nodes of their tenant" ON nodes;
DROP POLICY IF EXISTS "Users can delete nodes of their tenant" ON nodes;

-- Links
DROP POLICY IF EXISTS "Users can view links of their tenant" ON links;
DROP POLICY IF EXISTS "Users can insert links for their tenant" ON links;
DROP POLICY IF EXISTS "Users can update links of their tenant" ON links;

-- Pulses
DROP POLICY IF EXISTS "Users can view pulses of their tenant" ON pulses;
DROP POLICY IF EXISTS "Users can insert pulses for their tenant" ON pulses;

-- Patterns (table not created in current schema)
DO $$
BEGIN
  IF EXISTS (SELECT FROM pg_tables WHERE tablename = 'patterns') THEN
    DROP POLICY IF EXISTS "Users can view patterns of their tenant" ON patterns;
    DROP POLICY IF EXISTS "Users can insert patterns for their tenant" ON patterns;
  END IF;
END $$;

-- AI Insights (table not created in current schema)
DO $$
BEGIN
  IF EXISTS (SELECT FROM pg_tables WHERE tablename = 'ai_insights') THEN
    DROP POLICY IF EXISTS "Users can view ai_insights of their tenant" ON ai_insights;
    DROP POLICY IF EXISTS "Users can insert ai_insights for their tenant" ON ai_insights;
  END IF;
END $$;

-- ============================================================
-- STEP 2: Helper function to get tenant_id from auth.uid()
-- ============================================================

CREATE OR REPLACE FUNCTION get_user_tenant_id()
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

-- ============================================================
-- STEP 3: Tenants policies
-- Users can only see/update their own tenant
-- ============================================================

CREATE POLICY "tenants_select_own"
  ON tenants FOR SELECT
  USING (id = get_user_tenant_id());

CREATE POLICY "tenants_update_own"
  ON tenants FOR UPDATE
  USING (id = get_user_tenant_id())
  WITH CHECK (id = get_user_tenant_id());

-- ============================================================
-- STEP 4: Devices policies
-- Users can only access devices belonging to their tenant
-- ============================================================

CREATE POLICY "devices_select_own_tenant"
  ON devices FOR SELECT
  USING (tenant_id = get_user_tenant_id());

CREATE POLICY "devices_insert_own_tenant"
  ON devices FOR INSERT
  WITH CHECK (tenant_id = get_user_tenant_id());

CREATE POLICY "devices_update_own_tenant"
  ON devices FOR UPDATE
  USING (tenant_id = get_user_tenant_id())
  WITH CHECK (tenant_id = get_user_tenant_id());

CREATE POLICY "devices_delete_own_tenant"
  ON devices FOR DELETE
  USING (tenant_id = get_user_tenant_id());

-- ============================================================
-- STEP 5: Nodes policies
-- ============================================================

CREATE POLICY "nodes_select_own_tenant"
  ON nodes FOR SELECT
  USING (tenant_id = get_user_tenant_id());

CREATE POLICY "nodes_insert_own_tenant"
  ON nodes FOR INSERT
  WITH CHECK (tenant_id = get_user_tenant_id());

CREATE POLICY "nodes_update_own_tenant"
  ON nodes FOR UPDATE
  USING (tenant_id = get_user_tenant_id())
  WITH CHECK (tenant_id = get_user_tenant_id());

CREATE POLICY "nodes_delete_own_tenant"
  ON nodes FOR DELETE
  USING (tenant_id = get_user_tenant_id());

-- ============================================================
-- STEP 6: Links policies
-- ============================================================

CREATE POLICY "links_select_own_tenant"
  ON links FOR SELECT
  USING (tenant_id = get_user_tenant_id());

CREATE POLICY "links_insert_own_tenant"
  ON links FOR INSERT
  WITH CHECK (tenant_id = get_user_tenant_id());

CREATE POLICY "links_update_own_tenant"
  ON links FOR UPDATE
  USING (tenant_id = get_user_tenant_id())
  WITH CHECK (tenant_id = get_user_tenant_id());

CREATE POLICY "links_delete_own_tenant"
  ON links FOR DELETE
  USING (tenant_id = get_user_tenant_id());

-- ============================================================
-- STEP 7: Pulses policies
-- ============================================================

CREATE POLICY "pulses_select_own_tenant"
  ON pulses FOR SELECT
  USING (tenant_id = get_user_tenant_id());

CREATE POLICY "pulses_insert_own_tenant"
  ON pulses FOR INSERT
  WITH CHECK (tenant_id = get_user_tenant_id());

CREATE POLICY "pulses_update_own_tenant"
  ON pulses FOR UPDATE
  USING (tenant_id = get_user_tenant_id())
  WITH CHECK (tenant_id = get_user_tenant_id());

CREATE POLICY "pulses_delete_own_tenant"
  ON pulses FOR DELETE
  USING (tenant_id = get_user_tenant_id());

-- (patterns table not created - policies skipped)

-- ============================================================
-- STEP 8: Service role bypass (for Edge Functions)
-- ============================================================
-- Service role key bypasses RLS, so Edge Functions can access
-- all data. This is intentional and necessary.
-- ============================================================

-- ============================================================
-- STEP 9: Verify policies
-- ============================================================

DO $$
DECLARE
  policy_count INTEGER;
BEGIN
  SELECT COUNT(*) INTO policy_count
  FROM pg_policies
  WHERE schemaname = 'public';

  RAISE NOTICE 'Total RLS policies created: %', policy_count;
END $$;

-- ============================================================
-- STEP 10: Grant permissions
-- ============================================================

-- Grant USAGE on helper function
GRANT USAGE ON FUNCTION get_user_tenant_id() TO authenticated;
GRANT USAGE ON FUNCTION get_user_tenant_id() TO service_role;

-- ============================================================
-- COMPLETE!
-- ============================================================
SELECT '✅ Tenant-based RLS policies created!' AS status;
