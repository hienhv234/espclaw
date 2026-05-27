-- Migration: 006_cleanup_seed.sql
-- Reverts the damage from 004_seed_default_nodes.sql
-- Deletes all nodes/links/functions created by that bad seed.
-- Safe to run multiple times.

-- ============================================================
-- Step 1: Delete seeded nodes (the 6 abstract nodes)
-- ============================================================
DELETE FROM nodes
WHERE name IN (
    'User Voice',
    'Telegram Chat',
    'ESP Device',
    'LLM Brain',
    'Memory Store',
    'Reminders Hub'
);

-- ============================================================
-- Step 2: Delete seeded links
-- (links that reference the deleted nodes are auto-deleted by CASCADE,
--  but explicit cleanup is safer)
-- ============================================================
DELETE FROM links
WHERE id IN (
    -- Find links that had those nodes as source or target
    -- Since nodes are cascade-deleted above, links should be gone,
    -- but double-check by type+content
    SELECT l.id FROM links l
    WHERE l.content->>'description' IN (
        'Voice input triggers LLM processing',
        'Telegram message triggers LLM processing',
        'LLM creates reminders and appointments',
        'ESP device triggers and displays reminders',
        'LLM reads from persistent memory',
        'LLM writes learned facts to memory'
    )
);

-- ============================================================
-- Step 3: Drop the seed function
-- ============================================================
DROP FUNCTION IF EXISTS seed_default_nodes(UUID);
DROP FUNCTION IF EXISTS api_seed_user_nodes(UUID);
DROP FUNCTION IF EXISTS api_seed_user_nodes();

-- ============================================================
-- Step 4: Rollback enum additions from 004
-- Postgres does NOT allow removing enum values easily.
-- Only rollback if these values aren't used elsewhere.
-- Check first:
--   SELECT * FROM nodes WHERE type IN ('appointment');
--   SELECT * FROM links WHERE type IN ('creates','reads','writes');
-- If those return 0 rows, uncomment below:
-- ============================================================
-- To drop enum values safely, you need to recreate the type:
-- This is dangerous and version-specific. Safer to just ignore them.
-- The orphaned enum values cause no harm — they're just unused labels.
