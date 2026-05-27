-- Migration: 005_knowledge_layer.sql
-- ============================================================
-- ARCHITECTURE RULES
-- ============================================================
-- nodes/links = KNOWLEDGE LAYER
--   - Only entities that user actually mentions or creates
--   - Real-world things: people, projects, topics, events
--   - Semantic relationships: related_to, scheduled_by, part_of
--   - NO abstract channels, skills, or "memory store" nodes
--
-- Structured data → dedicated tables:
--   appointments, reminders, preferences, etc.
--
-- devices = TRANSPORT LAYER (devices table, NOT nodes)
-- ============================================================

-- ============================================================
-- STEP 1: Enums
-- ============================================================

DO $$ BEGIN
    ALTER TYPE node_type ADD VALUE IF NOT EXISTS 'entity';
    ALTER TYPE node_type ADD VALUE IF NOT EXISTS 'preference';
EXCEPTION WHEN duplicate_object THEN null;
END $$;

DO $$ BEGIN
    ALTER TYPE link_type ADD VALUE IF NOT EXISTS 'scheduled_by';
    ALTER TYPE link_type ADD VALUE IF NOT EXISTS 'prefers';
    ALTER TYPE link_type ADD VALUE IF NOT EXISTS 'participates_in';
EXCEPTION WHEN duplicate_object THEN null;
END $$;

-- ============================================================
-- STEP 2: Preferences table
-- User-level settings (NOT a node — it's structured config)
-- ============================================================
CREATE TABLE IF NOT EXISTS preferences (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    user_id         TEXT,
    key             TEXT NOT NULL,
    value           JSONB NOT NULL DEFAULT '{}',
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(tenant_id, user_id, key)
);

-- ============================================================
-- STEP 3: Appointments table
-- User-created scheduled events with time + channel
-- ============================================================
CREATE TABLE IF NOT EXISTS appointments (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
    title           TEXT NOT NULL,
    description     TEXT,
    scheduled_at    TIMESTAMPTZ NOT NULL,
    timezone        TEXT DEFAULT 'Asia/Ho_Chi_Minh',
    -- Channel: how to notify the user
    notify_channel   TEXT DEFAULT 'esp_voice' CHECK (notify_channel IN (
        'esp_voice',    -- ESP speaks the reminder
        'esp_display',  -- ESP shows on OLED
        'telegram',     -- Telegram bot message
        'esp_both',     -- ESP voice + display
        'all'           -- all channels
    )),
    -- Status
    status          TEXT DEFAULT 'pending' CHECK (status IN (
        'pending',   -- waiting for trigger time
        'triggered', -- reminder has fired
        'done',      -- user acknowledged
        'cancelled'  -- cancelled by user
    )),
    -- Source
    source_type     TEXT DEFAULT 'voice' CHECK (source_type IN (
        'voice',    -- spoken via ESP microphone
        'telegram', -- typed in Telegram
        'web',      -- created from dashboard
        'llm'       -- created by LLM planning
    )),
    -- Optional link to entity node (e.g., "call Mom")
    linked_entity_id    UUID REFERENCES nodes(id) ON DELETE SET NULL,
    -- Optional link to appointment node (for knowledge graph)
    linked_node_id      UUID REFERENCES nodes(id) ON DELETE SET NULL,
    -- Structured metadata
    metadata        JSONB DEFAULT '{}',
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================
-- STEP 4: Reminders table
-- Cron-triggerable records. One appointment may have multiple
-- reminder pulses (e.g., 1 hour before + at time).
-- ============================================================
CREATE TABLE IF NOT EXISTS reminders (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    tenant_id       UUID NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    appointment_id  UUID NOT NULL REFERENCES appointments(id) ON DELETE CASCADE,
    device_id       UUID REFERENCES devices(id) ON DELETE SET NULL,
    -- When to trigger this reminder
    trigger_at      TIMESTAMPTZ NOT NULL,
    -- Relative offset from appointment (minutes). NULL if at appointment time.
    offset_minutes  INTEGER,
    -- Status
    status          TEXT DEFAULT 'pending' CHECK (status IN (
        'pending',   -- waiting
        'sent',      -- notification sent to device
        'acknowledged', -- user saw it
        'missed',    -- passed without acknowledgement
        'cancelled'
    )),
    -- Which channel(s) confirmed delivery
    delivery_status JSONB DEFAULT '[]',
    sent_at         TIMESTAMPTZ,
    acknowledged_at TIMESTAMPTZ,
    created_at      TIMESTAMPTZ DEFAULT NOW(),
    updated_at      TIMESTAMPTZ DEFAULT NOW()
);

-- ============================================================
-- STEP 5: Entity nodes (knowledge graph)
-- Created on-demand when user mentions a person/concept.
-- These are the ONLY nodes that should be seeded/created.
-- ============================================================

-- entities view: just a convenience query over nodes
-- (no separate table needed — use nodes table with type='entity')

-- ============================================================
-- STEP 6: Indexes
-- ============================================================

CREATE INDEX idx_preferences_tenant ON preferences(tenant_id, user_id);
CREATE INDEX idx_preferences_key ON preferences(tenant_id, key);

CREATE INDEX idx_appointments_tenant ON appointments(tenant_id);
CREATE INDEX idx_appointments_scheduled ON appointments(tenant_id, scheduled_at) WHERE status = 'pending';
CREATE INDEX idx_appointments_status ON appointments(tenant_id, status) WHERE status = 'pending';
CREATE INDEX idx_appointments_device ON appointments(device_id) WHERE device_id IS NOT NULL;

CREATE INDEX idx_reminders_tenant ON reminders(tenant_id);
CREATE INDEX idx_reminders_trigger ON reminders(tenant_id, trigger_at) WHERE status = 'pending';
CREATE INDEX idx_reminders_appointment ON reminders(appointment_id);
CREATE INDEX idx_reminders_device ON reminders(device_id) WHERE device_id IS NOT NULL;

-- ============================================================
-- STEP 7: Helper functions
-- ============================================================

-- Create an entity node from an appointment (links knowledge → data)
CREATE OR REPLACE FUNCTION create_appointment_node(
    p_tenant_id     UUID,
    p_appointment_id UUID,
    p_title         TEXT
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
    node_id UUID;
BEGIN
    -- Check if node already exists for this appointment
    SELECT linked_node_id INTO node_id
    FROM appointments
    WHERE id = p_appointment_id;

    IF node_id IS NOT NULL THEN
        RETURN node_id;
    END IF;

    -- Create entity node
    INSERT INTO nodes (tenant_id, name, type, subtype, content, is_active)
    VALUES (
        p_tenant_id,
        p_title,
        'entity',
        'appointment',
        jsonb_build_object(
            'appointment_id', p_appointment_id,
            'scheduled_at', (SELECT scheduled_at FROM appointments WHERE id = p_appointment_id),
            'notify_channel', (SELECT notify_channel FROM appointments WHERE id = p_appointment_id),
            'status', 'pending'
        ),
        true
    )
    RETURNING id INTO node_id;

    -- Link appointment → node
    UPDATE appointments
    SET linked_node_id = node_id, updated_at = NOW()
    WHERE id = p_appointment_id;

    -- Create pulse: appointment_created
    INSERT INTO pulses (tenant_id, source_node_id, target_node_id, type, text, metadata)
    VALUES (
        p_tenant_id,
        NULL,
        node_id,
        'action',
        'Appointment created: ' || p_title,
        jsonb_build_object('appointment_id', p_appointment_id, 'action', 'created')
    );

    RETURN node_id;
END;
$$;

-- Get pending appointments for a device (for ESP sync)
CREATE OR REPLACE FUNCTION get_device_pending_appointments(
    p_device_id UUID
)
RETURNS TABLE (
    appointment_id UUID,
    title          TEXT,
    scheduled_at   TIMESTAMPTZ,
    notify_channel TEXT,
    reminder_count BIGINT
) LANGUAGE plpgsql SECURITY DEFINER SET search_path = public
AS $$
BEGIN
    RETURN QUERY
    SELECT
        a.id,
        a.title,
        a.scheduled_at,
        a.notify_channel,
        COUNT(r.id)::BIGINT AS reminder_count
    FROM appointments a
    LEFT JOIN reminders r ON r.appointment_id = a.id AND r.status = 'pending'
    WHERE a.device_id = p_device_id
      AND a.status = 'pending'
      AND a.scheduled_at > NOW()
    GROUP BY a.id, a.title, a.scheduled_at, a.notify_channel
    ORDER BY a.scheduled_at ASC;
END;
$$;

-- Get reminders due within time window (for ESP cron polling)
CREATE OR REPLACE FUNCTION get_due_reminders(
    p_device_id UUID,
    p_window_minutes INTEGER DEFAULT 5
)
RETURNS TABLE (
    reminder_id     UUID,
    appointment_id  UUID,
    title           TEXT,
    trigger_at      TIMESTAMPTZ,
    notify_channel  TEXT,
    description     TEXT
) LANGUAGE plpgsql SECURITY DEFINER SET search_path = public
AS $$
BEGIN
    RETURN QUERY
    SELECT
        r.id,
        r.appointment_id,
        a.title,
        r.trigger_at,
        a.notify_channel,
        COALESCE(a.description, ''::TEXT)
    FROM reminders r
    JOIN appointments a ON a.id = r.appointment_id
    WHERE r.device_id = p_device_id
      AND r.status = 'pending'
      AND r.trigger_at <= NOW() + (p_window_minutes || ' minutes')::INTERVAL
      AND r.trigger_at >= NOW() - (p_window_minutes || ' minutes')::INTERVAL
    ORDER BY r.trigger_at ASC;
END;
$$;

-- Acknowledge a reminder (called when ESP confirms user saw it)
CREATE OR REPLACE FUNCTION acknowledge_reminder(
    p_reminder_id UUID
)
RETURNS JSON
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
    v_reminder RECORD;
BEGIN
    UPDATE reminders
    SET status = 'acknowledged',
        acknowledged_at = NOW(),
        updated_at = NOW()
    WHERE id = p_reminder_id
    RETURNING * INTO v_reminder;

    -- Check if all reminders for this appointment are acknowledged
    IF NOT EXISTS (
        SELECT 1 FROM reminders
        WHERE appointment_id = v_reminder.appointment_id
          AND status = 'pending'
    ) THEN
        UPDATE appointments
        SET status = 'triggered', updated_at = NOW()
        WHERE id = v_reminder.appointment_id;
    END IF;

    RETURN jsonb_build_object(
        'ok', true,
        'reminder_id', p_reminder_id,
        'acknowledged_at', NOW()
    );
END;
$$;

-- Mark reminder as sent (ESP confirmed receipt, not user ack yet)
CREATE OR REPLACE FUNCTION mark_reminder_sent(
    p_reminder_id UUID,
    p_device_id   UUID
)
RETURNS JSON
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
BEGIN
    UPDATE reminders
    SET status = 'sent',
        sent_at = NOW(),
        updated_at = NOW(),
        delivery_status = jsonb_set(delivery_status, '{sent_via}',
            to_jsonb(p_device_id::TEXT))
    WHERE id = p_reminder_id;

    RETURN jsonb_build_object('ok', true, 'reminder_id', p_reminder_id);
END;
$$;

-- Get user preferences (safe wrapper)
CREATE OR REPLACE FUNCTION get_preference(
    p_tenant_id UUID,
    p_user_id   TEXT,
    p_key       TEXT
)
RETURNS JSONB
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
BEGIN
    RETURN (
        SELECT value FROM preferences
        WHERE tenant_id = p_tenant_id
          AND (user_id = p_user_id OR user_id IS NULL)
          AND key = p_key
        ORDER BY user_id DESC NULLS LAST
        LIMIT 1
    );
END;
$$;

-- Set user preference
CREATE OR REPLACE FUNCTION set_preference(
    p_tenant_id UUID,
    p_user_id   TEXT,
    p_key       TEXT,
    p_value     JSONB
)
RETURNS JSON
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
BEGIN
    INSERT INTO preferences (tenant_id, user_id, key, value)
    VALUES (p_tenant_id, p_user_id, p_key, p_value)
    ON CONFLICT (tenant_id, user_id, key)
    DO UPDATE SET value = p_value, updated_at = NOW();
    RETURN jsonb_build_object('ok', true, 'key', p_key);
END;
$$;

-- Create a reminder node in the knowledge graph (optional semantic link)
CREATE OR REPLACE FUNCTION create_entity_from_appointment(
    p_tenant_id     UUID,
    p_appointment_id UUID,
    p_entity_name   TEXT
)
RETURNS UUID
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = public
AS $$
DECLARE
    entity_node_id UUID;
    appt_node_id   UUID;
BEGIN
    -- Find or create the entity node (e.g., "Mom" from "call Mom at 3pm")
    SELECT id INTO entity_node_id
    FROM nodes
    WHERE tenant_id = p_tenant_id
      AND name = p_entity_name
      AND type = 'entity'
    LIMIT 1;

    IF entity_node_id IS NULL THEN
        INSERT INTO nodes (tenant_id, name, type, subtype, content, is_active)
        VALUES (p_tenant_id, p_entity_name, 'entity', 'person',
            jsonb_build_object('first_mentioned_at', NOW(), 'appointment_count', 1),
            true)
        RETURNING id INTO entity_node_id;
    ELSE
        UPDATE nodes
        SET content = content || jsonb_build_object('appointment_count',
            COALESCE((content->>'appointment_count')::int, 0) + 1),
            updated_at = NOW()
        WHERE id = entity_node_id;
    END IF;

    -- Get or create appointment node
    SELECT linked_node_id INTO appt_node_id
    FROM appointments
    WHERE id = p_appointment_id;

    IF appt_node_id IS NOT NULL THEN
        -- Link appointment node → entity node
        INSERT INTO links (tenant_id, source_node_id, target_node_id, type, weight)
        VALUES (p_tenant_id, appt_node_id, entity_node_id, 'scheduled_by', 1.0)
        ON CONFLICT DO NOTHING;
    END IF;

    -- Update appointment with entity link
    UPDATE appointments
    SET linked_entity_id = entity_node_id, updated_at = NOW()
    WHERE id = p_appointment_id;

    RETURN entity_node_id;
END;
$$;

-- ============================================================
-- STEP 8: RLS Policies
-- ============================================================

ALTER TABLE preferences ENABLE ROW LEVEL SECURITY;
ALTER TABLE appointments ENABLE ROW LEVEL SECURITY;
ALTER TABLE reminders   ENABLE ROW LEVEL SECURITY;

-- Preferences: tenant-scoped
CREATE POLICY preferences_tenant ON preferences
    FOR ALL USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

-- Appointments: tenant-scoped
CREATE POLICY appointments_tenant ON appointments
    FOR ALL USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

-- Reminders: tenant-scoped
CREATE POLICY reminders_tenant ON reminders
    FOR ALL USING (tenant_id = current_setting('app.current_tenant', TRUE)::UUID);

-- Public read for service_role (handled by SECURITY DEFINER on functions)

-- ============================================================
-- SUMMARY
-- ============================================================
-- nodes/links: ONLY for semantic entities (people, topics, events)
-- appointments: Structured time-bound events
-- reminders: Cron-triggerable notifications
-- preferences: Key-value user settings
--
-- NO abstract nodes like "User Voice", "LLM Brain", "Memory Store"
-- These are implementation details, not knowledge.
