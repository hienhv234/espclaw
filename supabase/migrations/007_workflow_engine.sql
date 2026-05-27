-- ============================================================
-- Migration: 007_workflow_engine.sql
-- Version: 007
-- Description: Workflow engine for programmable skill nodes
--              Enables user to create workflows on 3D graph that
--              ESP32 can execute as programmable skill logic.
-- ============================================================

-- ============================================================
-- STEP 1: Enums
-- ============================================================

DO $$ BEGIN
    CREATE TYPE workflow_trigger_type AS ENUM (
        'voice_command',  -- Text/regex match on voice input
        'scheduled',     -- Cron-like scheduling
        'event',         -- Triggered by another workflow or pulse
        'link_activated',-- Triggered when specific link pulses
        'llm_fallback',  -- Fallback when no workflow matches
        'manual'         -- Triggered manually from dashboard
    );
EXCEPTION WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
    CREATE TYPE workflow_step_type AS ENUM (
        'tool',           -- Call a registered ESP32 tool
        'llm',            -- Call LLM for reasoning/emotion analysis
        'condition',      -- Conditional branch (if/else)
        'wait',           -- Wait/delay
        'notify',         -- Send notification
        'create_node',    -- Create a new node in graph
        'update_node',    -- Update existing node
        'create_link',   -- Create link between nodes
        'http_request',   -- Make HTTP request
        'workflow'        -- Call another workflow (sub-workflow)
    );
EXCEPTION WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
    CREATE TYPE workflow_execution_status AS ENUM (
        'running',
        'completed',
        'failed',
        'cancelled',
        'pending'
    );
EXCEPTION WHEN duplicate_object THEN null;
END $$;

-- ============================================================
-- STEP 2: Add pattern_id to links table (fix auto-link edge function)
-- ============================================================

DO $$ BEGIN
    ALTER TABLE links ADD COLUMN pattern_id UUID;
EXCEPTION WHEN duplicate_object THEN null;
END $$;

CREATE INDEX IF NOT EXISTS idx_links_pattern ON links(pattern_id) WHERE pattern_id IS NOT NULL;

-- ============================================================
-- STEP 3: Add executable field to nodes table
-- This allows skill nodes to contain workflow definition
-- ============================================================

DO $$ BEGIN
    ALTER TABLE nodes ADD COLUMN executable JSONB;
EXCEPTION WHEN duplicate_object THEN null;
END $$;

COMMENT ON COLUMN nodes.executable IS 'Workflow definition for executable skill nodes (trigger + steps)';

-- ============================================================
-- STEP 4: Workflows table
-- The master workflow definition — sync-able to ESP32
-- ============================================================

CREATE TABLE IF NOT EXISTS workflows (
    id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    tenant_id           UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,

    -- Identity
    name                TEXT NOT NULL,
    description         TEXT,
    icon                TEXT DEFAULT '⚡',

    -- Classification
    category            TEXT DEFAULT 'general',  -- finance, health, home, automation
    subtype             TEXT,

    -- Trigger configuration
    trigger_type        workflow_trigger_type NOT NULL DEFAULT 'voice_command',
    trigger_pattern     TEXT,                   -- Regex or exact text pattern
    trigger_node_id     UUID REFERENCES nodes(id) ON DELETE SET NULL,
    -- For scheduled: cron expression
    -- For event: event type filter
    -- For link_activated: link_id filter
    trigger_config      JSONB DEFAULT '{}',     -- Additional trigger config

    -- Workflow steps (ordered array)
    -- Each step: { type, config, next_on_success, next_on_failure }
    steps               JSONB NOT NULL DEFAULT '[]',

    -- Error handling
    on_error            TEXT DEFAULT 'fallback_llm' CHECK (on_error IN (
        'skip', 'retry', 'fallback_llm', 'notify', 'stop'
    )),
    max_retries         INTEGER DEFAULT 3,

    -- Output mapping: how to update nodes after execution
    output_mapping      JSONB DEFAULT '[]',

    -- State
    is_enabled          BOOLEAN DEFAULT TRUE,
    priority            INTEGER DEFAULT 50,     -- Higher = runs first
    version             INTEGER DEFAULT 1,     -- Sync version

    -- Statistics
    trigger_count       INTEGER DEFAULT 0,
    success_count       INTEGER DEFAULT 0,
    failure_count       INTEGER DEFAULT 0,
    last_triggered_at   TIMESTAMPTZ,
    last_success_at     TIMESTAMPTZ,
    last_failure_at     TIMESTAMPTZ,
    avg_duration_ms     INTEGER DEFAULT 0,     -- Rolling average

    -- Provenance
    created_by          TEXT DEFAULT 'user',   -- user, device, ai
    source_device       UUID REFERENCES devices(id) ON DELETE SET NULL,

    -- Timestamps
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    UNIQUE(tenant_id, name)
);

-- ============================================================
-- STEP 5: Workflow Executions table (audit log)
-- Records every workflow execution for debugging + pattern detection
-- ============================================================

CREATE TABLE IF NOT EXISTS workflow_executions (
    id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    workflow_id         UUID NOT NULL REFERENCES workflows(id) ON DELETE CASCADE,
    tenant_id           UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    device_id           UUID REFERENCES devices(id) ON DELETE SET NULL,
    session_id          UUID,                              -- References sessions(id)

    -- Trigger info
    trigger_type        workflow_trigger_type NOT NULL,
    trigger_input       JSONB DEFAULT '{}',               -- What triggered this
    trigger_node_id     UUID,                             -- Which node triggered

    -- Execution state
    status              workflow_execution_status NOT NULL DEFAULT 'running',
    started_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    ended_at           TIMESTAMPTZ,

    -- Step tracking (which steps were executed)
    steps_executed      JSONB DEFAULT '[]',
    -- Example: [{ step: 0, type: 'tool', name: 'gpio_write', result: 'ok', ms: 12 }]

    -- Result
    result_data         JSONB DEFAULT '{}',               -- Output of workflow
    error_message       TEXT,
    error_step          INTEGER,                          -- Which step failed

    -- Performance
    duration_ms         INTEGER,                          -- Total duration
    llm_calls          INTEGER DEFAULT 0,                -- Count of LLM calls
    tool_calls         INTEGER DEFAULT 0,                -- Count of tool calls

    -- Pulse integration (creates pulse on execution)
    pulse_id           UUID,                             -- Created pulse record

    -- Timestamps
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ============================================================
-- STEP 6: Workflow execution history summary (fast queries)
-- ============================================================

CREATE TABLE IF NOT EXISTS workflow_stats (
    tenant_id           UUID PRIMARY KEY REFERENCES tenants(id) ON DELETE CASCADE,

    -- Per-workflow aggregates
    executions_today    INTEGER DEFAULT 0,
    executions_week     INTEGER DEFAULT 0,
    executions_total    INTEGER DEFAULT 0,
    success_rate        FLOAT DEFAULT 0,                 -- 0-1
    avg_duration_ms     INTEGER DEFAULT 0,

    -- Most triggered workflows
    top_workflow_ids   UUID[] DEFAULT '{}',

    -- Last updated
    computed_at        TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ============================================================
-- STEP 7: Indexes
-- ============================================================

-- Workflows indexes
CREATE INDEX IF NOT EXISTS idx_workflows_tenant ON workflows(tenant_id);
CREATE INDEX IF NOT EXISTS idx_workflows_category ON workflows(tenant_id, category);
CREATE INDEX IF NOT EXISTS idx_workflows_trigger_type ON workflows(tenant_id, trigger_type);
CREATE INDEX IF NOT EXISTS idx_workflows_enabled ON workflows(tenant_id, is_enabled) WHERE is_enabled = TRUE;
CREATE INDEX IF NOT EXISTS idx_workflows_priority ON workflows(tenant_id, priority DESC) WHERE is_enabled = TRUE;
CREATE INDEX IF NOT EXISTS idx_workflows_version ON workflows(tenant_id, version);

-- Workflow executions indexes
CREATE INDEX IF NOT EXISTS idx_executions_workflow ON workflow_executions(workflow_id);
CREATE INDEX IF NOT EXISTS idx_executions_tenant ON workflow_executions(tenant_id);
CREATE INDEX IF NOT EXISTS idx_executions_device ON workflow_executions(tenant_id, device_id) WHERE device_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_executions_status ON workflow_executions(workflow_id, status);
CREATE INDEX IF NOT EXISTS idx_executions_started ON workflow_executions(tenant_id, started_at DESC);
CREATE INDEX IF NOT EXISTS idx_executions_trigger ON workflow_executions(tenant_id, trigger_type, started_at DESC);

-- ============================================================
-- STEP 8: Helper Functions
-- ============================================================

-- Match a workflow against input text (for voice_command trigger)
CREATE OR REPLACE FUNCTION match_workflow_pattern(
    p_pattern TEXT,
    p_input TEXT
)
RETURNS BOOLEAN
LANGUAGE plpgsql
IMMUTABLE
AS $$
BEGIN
    -- If pattern starts with ^, treat as regex
    IF p_pattern LIKE '^%' THEN
        RETURN p_input ~* p_pattern;
    ELSE
        -- Case-insensitive contains match
        RETURN p_input ILIKE '%' || p_pattern || '%';
    END IF;
END;
$$;

-- Find matching workflows for input (ordered by priority)
CREATE OR REPLACE FUNCTION find_matching_workflows(
    p_tenant_id UUID,
    p_trigger_type workflow_trigger_type,
    p_input TEXT DEFAULT NULL,
    p_trigger_node_id UUID DEFAULT NULL
)
RETURNS TABLE (
    workflow_id UUID,
    name TEXT,
    trigger_pattern TEXT,
    priority INTEGER,
    match_score FLOAT
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        w.id,
        w.name,
        w.trigger_pattern,
        w.priority,
        CASE
            WHEN p_input IS NOT NULL AND w.trigger_pattern IS NOT NULL
                THEN CASE
                    WHEN match_workflow_pattern(w.trigger_pattern, p_input) THEN 1.0
                    ELSE 0.0
                END
            ELSE 1.0
        END AS match_score
    FROM workflows w
    WHERE w.tenant_id = p_tenant_id
      AND w.is_enabled = TRUE
      AND w.trigger_type = p_trigger_type
      AND (
          -- For voice_command: text must match pattern
          (p_trigger_type = 'voice_command' AND w.trigger_pattern IS NOT NULL AND match_workflow_pattern(w.trigger_pattern, COALESCE(p_input, '')))
          OR
          -- For event/link_activated: must match trigger node
          (p_trigger_type IN ('event', 'link_activated') AND (p_trigger_node_id IS NULL OR w.trigger_node_id = p_trigger_node_id))
          OR
          -- For scheduled/manual: no filter needed
          (p_trigger_type IN ('scheduled', 'manual', 'llm_fallback'))
      )
    ORDER BY w.priority DESC, match_score DESC;
END;
$$ LANGUAGE plpgsql STABLE;

-- Record workflow execution start
CREATE OR REPLACE FUNCTION workflow_execution_start(
    p_workflow_id UUID,
    p_tenant_id UUID,
    p_device_id UUID,
    p_trigger_type workflow_trigger_type,
    p_trigger_input JSONB DEFAULT '{}',
    p_trigger_node_id UUID DEFAULT NULL,
    p_session_id UUID DEFAULT NULL
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
    v_exec_id UUID;
BEGIN
    INSERT INTO workflow_executions (
        workflow_id, tenant_id, device_id, session_id,
        trigger_type, trigger_input, trigger_node_id, status
    ) VALUES (
        p_workflow_id, p_tenant_id, p_device_id, p_session_id,
        p_trigger_type, p_trigger_input, p_trigger_node_id, 'running'
    )
    RETURNING id INTO v_exec_id;

    -- Update workflow stats
    UPDATE workflows
    SET
        trigger_count = trigger_count + 1,
        last_triggered_at = NOW()
    WHERE id = p_workflow_id;

    RETURN v_exec_id;
END;
$$;

-- Record workflow execution end
CREATE OR REPLACE FUNCTION workflow_execution_end(
    p_execution_id UUID,
    p_status workflow_execution_status,
    p_steps_executed JSONB DEFAULT '[]',
    p_result_data JSONB DEFAULT '{}',
    p_error_message TEXT DEFAULT NULL,
    p_error_step INTEGER DEFAULT NULL,
    p_duration_ms INTEGER DEFAULT NULL,
    p_llm_calls INTEGER DEFAULT 0,
    p_tool_calls INTEGER DEFAULT 0,
    p_pulse_id UUID DEFAULT NULL
)
RETURNS VOID
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
    v_workflow_id UUID;
BEGIN
    -- Update execution record
    UPDATE workflow_executions
    SET
        status = p_status,
        steps_executed = p_steps_executed,
        result_data = p_result_data,
        error_message = p_error_message,
        error_step = p_error_step,
        duration_ms = p_duration_ms,
        llm_calls = p_llm_calls,
        tool_calls = p_tool_calls,
        pulse_id = p_pulse_id,
        ended_at = NOW()
    WHERE id = p_execution_id
    RETURNING workflow_id INTO v_workflow_id;

    -- Update workflow stats
    UPDATE workflows
    SET
        success_count = CASE WHEN p_status = 'completed' THEN success_count + 1 ELSE success_count END,
        failure_count = CASE WHEN p_status = 'failed' THEN failure_count + 1 ELSE failure_count END,
        last_success_at = CASE WHEN p_status = 'completed' THEN NOW() ELSE last_success_at END,
        last_failure_at = CASE WHEN p_status = 'failed' THEN NOW() ELSE last_failure_at END,
        avg_duration_ms = CASE
            WHEN p_duration_ms IS NOT NULL
            THEN (avg_duration_ms * trigger_count + p_duration_ms) / (trigger_count + 1)
            ELSE avg_duration_ms
        END
    WHERE id = v_workflow_id;
END;
$$;

-- Get workflows for ESP32 sync (enabled + version-based delta)
CREATE OR REPLACE FUNCTION get_workflows_for_sync(
    p_tenant_id UUID,
    p_device_id UUID,
    p_since_version INTEGER DEFAULT 0
)
RETURNS TABLE (
    id UUID,
    name TEXT,
    description TEXT,
    icon TEXT,
    category TEXT,
    trigger_type TEXT,
    trigger_pattern TEXT,
    trigger_node_id UUID,
    trigger_config JSONB,
    steps JSONB,
    on_error TEXT,
    max_retries INTEGER,
    output_mapping JSONB,
    priority INTEGER,
    version INTEGER
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        w.id,
        w.name,
        w.description,
        w.icon,
        w.category,
        w.trigger_type::TEXT,
        w.trigger_pattern,
        w.trigger_node_id,
        w.trigger_config,
        w.steps,
        w.on_error,
        w.max_retries,
        w.output_mapping,
        w.priority,
        w.version
    FROM workflows w
    WHERE w.tenant_id = p_tenant_id
      AND w.is_enabled = TRUE
      AND w.version > p_since_version;
END;
$$ LANGUAGE plpgsql STABLE;

-- Get workflow execution history (for debug console)
CREATE OR REPLACE FUNCTION get_workflow_execution_history(
    p_tenant_id UUID,
    p_workflow_id UUID DEFAULT NULL,
    p_limit INTEGER DEFAULT 50
)
RETURNS TABLE (
    execution_id UUID,
    workflow_id UUID,
    workflow_name TEXT,
    trigger_type TEXT,
    status TEXT,
    started_at TIMESTAMPTZ,
    duration_ms INTEGER,
    error_message TEXT,
    steps_summary JSONB
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        we.id,
        we.workflow_id,
        w.name,
        we.trigger_type::TEXT,
        we.status::TEXT,
        we.started_at,
        we.duration_ms,
        we.error_message,
        we.steps_executed
    FROM workflow_executions we
    JOIN workflows w ON w.id = we.workflow_id
    WHERE we.tenant_id = p_tenant_id
      AND (p_workflow_id IS NULL OR we.workflow_id = p_workflow_id)
    ORDER BY we.started_at DESC
    LIMIT p_limit;
END;
$$ LANGUAGE plpgsql STABLE;

-- ============================================================
-- STEP 9: RLS Policies
-- ============================================================

ALTER TABLE workflows ENABLE ROW LEVEL SECURITY;
ALTER TABLE workflow_executions ENABLE ROW LEVEL SECURITY;
ALTER TABLE workflow_stats ENABLE ROW LEVEL SECURITY;

-- Workflows: tenant isolation
CREATE POLICY workflows_select ON workflows
    FOR SELECT USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

CREATE POLICY workflows_insert ON workflows
    FOR INSERT WITH CHECK (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

CREATE POLICY workflows_update ON workflows
    FOR UPDATE USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

CREATE POLICY workflows_delete ON workflows
    FOR DELETE USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

-- Workflow executions: tenant isolation
CREATE POLICY executions_select ON workflow_executions
    FOR SELECT USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

CREATE POLICY executions_insert ON workflow_executions
    FOR INSERT WITH CHECK (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

CREATE POLICY executions_update ON workflow_executions
    FOR UPDATE USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

-- Workflow stats: tenant isolation
CREATE POLICY stats_select ON workflow_stats
    FOR SELECT USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

CREATE POLICY stats_upsert ON workflow_stats
    FOR INSERT WITH CHECK (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

CREATE POLICY stats_update ON workflow_stats
    FOR UPDATE USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

-- ============================================================
-- STEP 10: Realtime
-- ============================================================

-- Enable realtime for workflow executions
ALTER PUBLICATION supabase_realtime ADD TABLE workflow_executions;

-- ============================================================
-- STEP 11: Comments
-- ============================================================

COMMENT ON TABLE workflows IS 'Executable skill workflows — programmable logic nodes sync-able to ESP32';
COMMENT ON TABLE workflow_executions IS 'Audit log of every workflow execution for debugging + pattern detection';
COMMENT ON COLUMN workflows.steps IS 'JSONB array of workflow steps. Each step: { id, type, config, next_on_success, next_on_failure, description }';
COMMENT ON COLUMN workflows.trigger_pattern IS 'Regex pattern (^...) or text pattern for voice_command matching';
COMMENT ON COLUMN workflows.trigger_config IS 'Additional trigger config: { cron: "*/5 * * * *", event_types: [...], link_id: "uuid" }';
COMMENT ON COLUMN workflows.output_mapping IS 'Array of { target_node_id, field, value } to update after execution';

-- ============================================================
-- EXAMPLE WORKFLOW: Finance tracking (for testing)
-- ============================================================

-- INSERT example (run manually after migration):
/*
INSERT INTO workflows (tenant_id, name, description, category, icon, trigger_type, trigger_pattern, steps, on_error, priority)
VALUES (
    '00000000-0000-0000-0000-000000000001',
    'Finance_TrackSpending',
    'Track spending and update budget when transaction keywords detected',
    'finance',
    '💰',
    'voice_command',
    'chi|tiêu|mua|thanh toán',
    '[
        {
            "id": 0,
            "type": "condition",
            "description": "Check if amount was mentioned",
            "config": {
                "field": "has_amount",
                "operator": "eq",
                "value": true
            },
            "next_on_success": 1,
            "next_on_failure": 3
        },
        {
            "id": 1,
            "type": "create_node",
            "description": "Create transaction node",
            "config": {
                "node_type": "transaction",
                "name_template": "{{input}}",
                "content": {
                    "amount": "{{extract_amount}}",
                    "category": "{{extract_category}}"
                }
            },
            "next_on_success": 2,
            "next_on_failure": 3
        },
        {
            "id": 2,
            "type": "llm",
            "description": "Generate spending insight",
            "config": {
                "prompt": "Given the transaction: {{input}}. Provide a brief financial insight.",
                "fallback": "Transaction recorded."
            },
            "next_on_success": null,
            "next_on_failure": null
        },
        {
            "id": 3,
            "type": "notify",
            "description": "Confirm to user",
            "config": {
                "message": "Transaction tracked: {{input}}",
                "channel": "esp_voice"
            },
            "next_on_success": null,
            "next_on_failure": null
        }
    ]'::jsonb,
    'fallback_llm',
    80
);
*/
