-- ============================================================
-- NEUTRON-ESPClaw: Realtime Functions & API Helpers
-- Version: 003
-- Description: Functions for real-time sync, pattern detection, auto-linking
-- ============================================================

-- ============================================================
-- SEMANTIC SEARCH FUNCTIONS
-- ============================================================

-- Find semantically similar nodes using vector similarity
CREATE OR REPLACE FUNCTION find_similar_nodes(
  p_tenant_id UUID,
  p_embedding VECTOR(1536),
  p_limit INTEGER DEFAULT 10,
  p_threshold FLOAT DEFAULT 0.7,
  p_exclude_types TEXT[] DEFAULT NULL
)
RETURNS TABLE (
  id UUID,
  name TEXT,
  type TEXT,
  subtype TEXT,
  similarity FLOAT,
  pos_x FLOAT,
  pos_y FLOAT,
  pos_z FLOAT
) AS $$
BEGIN
  RETURN QUERY
  SELECT
    n.id,
    n.name,
    n.type::TEXT,
    n.subtype,
    1 - (n.embedding <=> p_embedding) AS similarity,
    n.pos_x,
    n.pos_y,
    n.pos_z
  FROM nodes n
  WHERE n.tenant_id = p_tenant_id
    AND n.is_active = TRUE
    AND n.embedding IS NOT NULL
    AND 1 - (n.embedding <=> p_embedding) >= p_threshold
    AND (p_exclude_types IS NULL OR n.type::TEXT != ALL(p_exclude_types))
  ORDER BY n.embedding <=> p_embedding
  LIMIT p_limit;
END;
$$ LANGUAGE plpgsql STABLE;

-- Find nodes matching natural language query
CREATE OR REPLACE FUNCTION search_nodes(
  p_tenant_id UUID,
  p_query TEXT,
  p_limit INTEGER DEFAULT 20
)
RETURNS TABLE (
  id UUID,
  name TEXT,
  type TEXT,
  subtype TEXT,
  description TEXT,
  relevance FLOAT
) AS $$
BEGIN
  RETURN QUERY
  SELECT
    n.id,
    n.name,
    n.type::TEXT,
    n.subtype,
    n.description,
    ts_rank(
      to_tsvector('english', COALESCE(n.name, '') || ' ' || COALESCE(n.description, '')),
      plainto_tsquery('english', p_query)
    ) AS relevance
  FROM nodes n
  WHERE n.tenant_id = p_tenant_id
    AND n.is_active = TRUE
    AND (
      to_tsvector('english', COALESCE(n.name, '') || ' ' || COALESCE(n.description, '')) @@ plainto_tsquery('english', p_query)
      OR n.name ILIKE '%' || p_query || '%'
    )
  ORDER BY relevance DESC, n.activation_count DESC
  LIMIT p_limit;
END;
$$ LANGUAGE plpgsql STABLE;

-- ============================================================
-- GRAPH TRAVERSAL FUNCTIONS
-- ============================================================

-- Get all linked nodes within N hops
CREATE OR REPLACE FUNCTION get_connected_nodes(
  p_tenant_id UUID,
  p_node_id UUID,
  p_hops INTEGER DEFAULT 2,
  p_link_types TEXT[] DEFAULT NULL,
  p_direction TEXT DEFAULT 'both'  -- 'outgoing', 'incoming', 'both'
)
RETURNS TABLE (
  node_id UUID,
  node_name TEXT,
  node_type TEXT,
  depth INTEGER,
  path UUID[]
) AS $$
WITH RECURSIVE graph_traverse AS (
  -- Base case: starting node
  SELECT
    n.id AS node_id,
    n.name AS node_name,
    n.type::TEXT AS node_type,
    0 AS depth,
    ARRAY[n.id] AS path
  FROM nodes n
  WHERE n.id = p_node_id AND n.tenant_id = p_tenant_id
  
  UNION ALL
  
  -- Recursive case: traverse links
  SELECT
    n.id AS node_id,
    n.name AS node_name,
    n.type::TEXT AS node_type,
    gt.depth + 1 AS depth,
    gt.path || n.id AS path
  FROM graph_traverse gt
  JOIN links l ON (
    (p_direction IN ('outgoing', 'both') AND l.source_node_id = gt.node_id)
    OR (p_direction IN ('incoming', 'both') AND l.target_node_id = gt.node_id)
  )
  JOIN nodes n ON (
    (p_direction IN ('outgoing', 'both') AND n.id = l.target_node_id)
    OR (p_direction IN ('incoming', 'both') AND n.id = l.source_node_id)
  )
  WHERE gt.depth < p_hops
    AND n.tenant_id = p_tenant_id
    AND n.is_active = TRUE
    AND l.is_active = TRUE
    AND n.id != ALL(gt.path)  -- Prevent cycles
    AND (p_link_types IS NULL OR l.type::TEXT = ANY(p_link_types))
)
SELECT DISTINCT ON (node_id) * FROM graph_traverse
ORDER BY node_id, depth;
$$ LANGUAGE plpgsql STABLE;

-- Find path between two nodes (BFS)
CREATE OR REPLACE FUNCTION find_path(
  p_tenant_id UUID,
  p_source_id UUID,
  p_target_id UUID,
  p_max_depth INTEGER DEFAULT 4
)
RETURNS TABLE (
  path UUID[],
  total_weight FLOAT
) AS $$
WITH RECURSIVE path_finder AS (
  SELECT
    ARRAY[l.source_node_id] AS path,
    l.weight AS total_weight,
    l.target_node_id AS current,
    1 AS depth
  FROM links l
  WHERE l.source_node_id = p_source_id
    AND l.tenant_id = p_tenant_id
    AND l.is_active = TRUE
  
  UNION ALL
  
  SELECT
    pf.path || l.target_node_id,
    pf.total_weight * l.weight,
    l.target_node_id,
    pf.depth + 1
  FROM path_finder pf
  JOIN links l ON l.source_node_id = pf.current
  WHERE pf.depth < p_max_depth
    AND l.tenant_id = p_tenant_id
    AND l.is_active = TRUE
    AND NOT l.target_node_id = ANY(pf.path)  -- No cycles
)
SELECT pf.path || p_target_id, pf.total_weight
FROM path_finder pf
WHERE pf.current = p_target_id
ORDER BY pf.total_weight DESC
LIMIT 1;
$$ LANGUAGE plpgsql STABLE;

-- ============================================================
-- AUTO-LINKING & PATTERN DETECTION
-- ============================================================

-- Detect behavioral patterns (co-occurrence)
CREATE OR REPLACE FUNCTION detect_cooccurrence_patterns(
  p_tenant_id UUID,
  p_time_window_hours INTEGER DEFAULT 2,
  p_min_frequency INTEGER DEFAULT 3
)
RETURNS TABLE (
  before_node_id UUID,
  before_node_name TEXT,
  after_node_id UUID,
  after_node_name TEXT,
  link_type TEXT,
  frequency INTEGER,
  confidence FLOAT,
  avg_time_gap_seconds FLOAT,
  last_seen TIMESTAMPTZ
) AS $$
BEGIN
  RETURN QUERY
  WITH pulse_pairs AS (
    SELECT
      p1.id AS pulse1_id,
      p1.created_at AS time1,
      p2.id AS pulse2_id,
      p2.created_at AS time2,
      p1.triggered_nodes AS nodes1,
      p2.triggered_nodes AS nodes2
    FROM pulses p1
    JOIN pulses p2 ON 
      p1.tenant_id = p2.tenant_id
      AND p2.created_at > p1.created_at
      AND p2.created_at <= p1.created_at + (p_time_window_hours || ' hours')::INTERVAL
    WHERE p1.tenant_id = p_tenant_id
  ),
  node_pairs AS (
    SELECT DISTINCT
      n1 AS before_id,
      n2 AS after_id,
      COUNT(*) AS frequency,
      MAX(pp.time2) AS last_seen,
      AVG(EXTRACT(EPOCH FROM (pp.time2 - pp.time1))) AS avg_gap
    FROM pulse_pairs pp
    CROSS JOIN UNNEST(pp.nodes1) AS t1(n1)
    CROSS JOIN UNNEST(pp.nodes2) AS t2(n2)
    WHERE n1 != n2
    GROUP BY n1, n2
    HAVING COUNT(*) >= p_min_frequency
  )
  SELECT
    np.before_id,
    nb.name,
    np.after_id,
    na.name,
    'followed_by'::TEXT AS link_type,
    np.frequency,
    LEAST(1.0, np.frequency::FLOAT / 10)::FLOAT AS confidence,  -- Cap at 1.0
    np.avg_gap,
    np.last_seen
  FROM node_pairs np
  JOIN nodes nb ON nb.id = np.before_id AND nb.tenant_id = p_tenant_id
  JOIN nodes na ON na.id = np.after_id AND na.tenant_id = p_tenant_id
  ORDER BY np.frequency DESC;
END;
$$ LANGUAGE plpgsql STABLE;

-- Create auto-links based on detected patterns
CREATE OR REPLACE FUNCTION create_auto_links_from_patterns(
  p_tenant_id UUID,
  p_min_confidence FLOAT DEFAULT 0.7,
  p_max_new_links INTEGER DEFAULT 50
)
RETURNS TABLE (
  link_id UUID,
  source_id UUID,
  target_id UUID,
  link_type TEXT,
  weight FLOAT
) AS $$
DECLARE
  pattern RECORD;
  created_count INTEGER := 0;
BEGIN
  FOR pattern IN (
    SELECT * FROM detect_cooccurrence_patterns(
      p_tenant_id,
      2,        -- 2 hour window
      3         -- min 3 occurrences
    ) cp
    WHERE cp.confidence >= p_min_confidence
    LIMIT p_max_new_links
  ) LOOP
    -- Check if link already exists
    IF NOT EXISTS (
      SELECT 1 FROM links
      WHERE tenant_id = p_tenant_id
        AND source_node_id = pattern.before_node_id
        AND target_node_id = pattern.after_node_id
        AND type = 'followed_by'::link_type
    ) THEN
      -- Create new link
      INSERT INTO links (
        tenant_id, source_node_id, target_node_id, type,
        weight, confidence, is_ai_generated, created_by
      ) VALUES (
        p_tenant_id,
        pattern.before_node_id,
        pattern.after_node_id,
        'followed_by',
        pattern.confidence,
        pattern.confidence,
        TRUE,
        'ai_pattern_detector'
      )
      ON CONFLICT (tenant_id, source_node_id, target_node_id, type) DO UPDATE
      SET weight = GREATEST(links.weight, EXCLUDED.weight),
          confidence = GREATEST(links.confidence, EXCLUDED.confidence),
          is_ai_generated = TRUE;
      
      created_count := created_count + 1;
      
      RETURN QUERY
      SELECT 
        links.id,
        links.source_node_id,
        links.target_node_id,
        links.type::TEXT,
        links.weight
      FROM links
      WHERE links.source_node_id = pattern.before_node_id
        AND links.target_node_id = pattern.after_node_id
        AND links.type = 'followed_by';
    END IF;
  END LOOP;
  
  RAISE NOTICE 'Created % auto-links', created_count;
END;
$$ LANGUAGE plpgsql;

-- ============================================================
-- SPATIAL POSITIONING FUNCTIONS
-- ============================================================

-- Calculate optimal position for new node based on neighbors
CREATE OR REPLACE FUNCTION calculate_node_position(
  p_tenant_id UUID,
  p_node_type TEXT,
  p_subtype TEXT DEFAULT NULL,
  p_importance FLOAT DEFAULT 0.5,
  p_domain_y FLOAT DEFAULT NULL
)
RETURNS TABLE (
  pos_x FLOAT,
  pos_y FLOAT,
  pos_z FLOAT
) AS $$
DECLARE
  domain_min_y FLOAT;
  domain_max_y FLOAT;
  avg_x FLOAT;
  avg_z FLOAT;
  variance_x FLOAT;
  variance_z FLOAT;
BEGIN
  -- Get domain range based on node type
  SELECT 
    CASE p_node_type
      WHEN 'user' THEN 0
      WHEN 'device' THEN 50
      WHEN 'context' THEN 200
      WHEN 'finance' THEN 300
      WHEN 'health' THEN 400
      WHEN 'home' THEN 500
      WHEN 'social' THEN 600
      WHEN 'knowledge' THEN 700
      WHEN 'creative' THEN 800
      WHEN 'memory' THEN 900
      ELSE 500
    END,
    CASE p_node_type
      WHEN 'user' THEN 99
      WHEN 'device' THEN 149
      WHEN 'context' THEN 299
      WHEN 'finance' THEN 399
      WHEN 'health' THEN 499
      WHEN 'home' THEN 599
      WHEN 'social' THEN 699
      WHEN 'knowledge' THEN 799
      WHEN 'creative' THEN 899
      WHEN 'memory' THEN 999
      ELSE 599
    END
  INTO domain_min_y, domain_max_y;
  
  -- Override with explicit domain if provided
  IF p_domain_y IS NOT NULL THEN
    domain_min_y := p_domain_y;
    domain_max_y := p_domain_y;
  END IF;
  
  -- Calculate average position of similar nodes for clustering
  SELECT 
    AVG(pos_x),
    AVG(pos_z),
    STDDEV(pos_x),
    STDDEV(pos_z)
  INTO avg_x, avg_z, variance_x, variance_z
  FROM nodes
  WHERE tenant_id = p_tenant_id
    AND type::TEXT = p_node_type
    AND is_active = TRUE;
  
  -- Add jitter to prevent overlap, scaled by importance
  pos_x := COALESCE(avg_x, 50) + (random() - 0.5) * 20 * (1 - p_importance);
  pos_y := domain_min_y + (random() * (domain_max_y - domain_min_y));
  pos_z := COALESCE(avg_z, 50) + (random() - 0.5) * 20 * (1 - p_importance);
  
  -- Clamp values
  pos_x := GREATEST(0, LEAST(100, pos_x));
  pos_y := GREATEST(0, LEAST(999, pos_y));
  pos_z := GREATEST(0, LEAST(100, pos_z));
  
  RETURN NEXT;
END;
$$ LANGUAGE plpgsql STABLE;

-- ============================================================
-- SYNC HELPER FUNCTIONS
-- ============================================================

-- Get nodes changed since version
CREATE OR REPLACE FUNCTION get_nodes_since_version(
  p_tenant_id UUID,
  p_device_id UUID,
  p_version INTEGER
)
RETURNS TABLE (
  id UUID,
  type TEXT,
  subtype TEXT,
  name TEXT,
  description TEXT,
  content JSONB,
  pos_x FLOAT,
  pos_y FLOAT,
  pos_z FLOAT,
  is_active BOOLEAN,
  is_pinned BOOLEAN,
  is_favorite BOOLEAN,
  version INTEGER,
  updated_at TIMESTAMPTZ
) AS $$
BEGIN
  RETURN QUERY
  SELECT
    n.id,
    n.type::TEXT,
    n.subtype,
    n.name,
    n.description,
    n.content,
    n.pos_x,
    n.pos_y,
    n.pos_z,
    n.is_active,
    n.is_pinned,
    n.is_favorite,
    n.version,
    n.updated_at
  FROM nodes n
  WHERE n.tenant_id = p_tenant_id
    AND n.version > p_version
  ORDER BY n.updated_at ASC;
END;
$$ LANGUAGE plpgsql STABLE;

-- Get links changed since version
CREATE OR REPLACE FUNCTION get_links_since_version(
  p_tenant_id UUID,
  p_device_id UUID,
  p_version INTEGER
)
RETURNS TABLE (
  id UUID,
  source_node_id UUID,
  target_node_id UUID,
  type TEXT,
  weight FLOAT,
  confidence FLOAT,
  is_active BOOLEAN,
  version INTEGER,
  updated_at TIMESTAMPTZ
) AS $$
BEGIN
  RETURN QUERY
  SELECT
    l.id,
    l.source_node_id,
    l.target_node_id,
    l.type::TEXT,
    l.weight,
    l.confidence,
    l.is_active,
    l.version,
    l.updated_at
  FROM links l
  WHERE l.tenant_id = p_tenant_id
    AND l.version > p_version
  ORDER BY l.updated_at ASC;
END;
$$ LANGUAGE plpgsql STABLE;

-- Bulk update node versions (for sync acknowledgment)
CREATE OR REPLACE FUNCTION acknowledge_sync(
  p_tenant_id UUID,
  p_device_id UUID,
  p_synced_node_versions JSONB,
  p_synced_link_versions JSONB
)
RETURNS VOID AS $$
DECLARE
  node_rec JSONB;
  link_rec JSONB;
BEGIN
  -- Update synced nodes
  FOR node_rec IN SELECT * FROM jsonb_array_elements(p_synced_node_versions)
  LOOP
    UPDATE nodes
    SET sqlite_version = sqlite_version + 1
    WHERE id = node_rec->>'id'
      AND tenant_id = p_tenant_id;
  END LOOP;
  
  -- Update synced links
  FOR link_rec IN SELECT * FROM jsonb_array_elements(p_synced_link_versions)
  LOOP
    UPDATE links
    SET sqlite_version = sqlite_version + 1
    WHERE id = link_rec->>'id'
      AND tenant_id = p_tenant_id;
  END LOOP;
END;
$$ LANGUAGE plpgsql;

-- ============================================================
-- PULSE RECORDING FUNCTIONS
-- ============================================================

-- Record a pulse and update linked node statistics
CREATE OR REPLACE FUNCTION record_pulse(
  p_tenant_id UUID,
  p_device_id UUID,
  p_session_id UUID,
  p_pulse_type pulse_type,
  p_content JSONB,
  p_triggered_nodes UUID[] DEFAULT '{}',
  p_latency_ms INTEGER DEFAULT NULL
)
RETURNS UUID AS $$
DECLARE
  new_pulse_id UUID;
BEGIN
  -- Insert pulse
  INSERT INTO pulses (
    tenant_id, device_id, session_id, pulse_type,
    content, triggered_nodes, latency_ms
  ) VALUES (
    p_tenant_id, p_device_id, p_session_id, p_pulse_type,
    p_content, p_triggered_nodes, p_latency_ms
  )
  RETURNING id INTO new_pulse_id;
  
  -- Update triggered nodes' activation count and pulse strength
  UPDATE nodes
  SET 
    activation_count = activation_count + 1,
    pulse_strength = LEAST(1.0, pulse_strength + 0.1),
    accessed_at = NOW()
  WHERE id = ANY(p_triggered_nodes)
    AND tenant_id = p_tenant_id;
  
  -- Update links that were traversed
  UPDATE links
  SET 
    last_pulsed_at = NOW(),
    weight = LEAST(1.0, weight + 0.01)  -- Slightly increase weight on use
  WHERE source_node_id = ANY(p_triggered_nodes)
    OR target_node_id = ANY(p_triggered_nodes)
    AND tenant_id = p_tenant_id;
  
  RETURN new_pulse_id;
END;
$$ LANGUAGE plpgsql;

-- ============================================================
-- GRAPH STATISTICS FUNCTIONS
-- ============================================================

-- Recalculate graph statistics for a tenant
CREATE OR REPLACE FUNCTION recalculate_graph_stats(p_tenant_id UUID)
RETURNS VOID AS $$
DECLARE
  stats JSONB;
BEGIN
  -- Count nodes by type
  SELECT jsonb_object_agg(node_type, count)
  INTO stats.nodes_by_type
  FROM (
    SELECT type::TEXT as node_type, COUNT(*) as count
    FROM nodes
    WHERE tenant_id = p_tenant_id AND is_active = TRUE
    GROUP BY type
  ) t;
  
  -- Count links by type
  SELECT jsonb_object_agg(link_type, count)
  INTO stats.links_by_type
  FROM (
    SELECT type::TEXT as link_type, COUNT(*) as count
    FROM links
    WHERE tenant_id = p_tenant_id AND is_active = TRUE
    GROUP BY type
  ) t;
  
  -- Count by domain (Y position ranges)
  SELECT jsonb_build_object(
    'system', COUNT(*) FILTER (WHERE pos_y < 100),
    'device', COUNT(*) FILTER (WHERE pos_y >= 50 AND pos_y < 150),
    'context', COUNT(*) FILTER (WHERE pos_y >= 200 AND pos_y < 300),
    'finance', COUNT(*) FILTER (WHERE pos_y >= 300 AND pos_y < 400),
    'health', COUNT(*) FILTER (WHERE pos_y >= 400 AND pos_y < 500),
    'home', COUNT(*) FILTER (WHERE pos_y >= 500 AND pos_y < 600),
    'social', COUNT(*) FILTER (WHERE pos_y >= 600 AND pos_y < 700),
    'knowledge', COUNT(*) FILTER (WHERE pos_y >= 700 AND pos_y < 800),
    'creative', COUNT(*) FILTER (WHERE pos_y >= 800 AND pos_y < 900),
    'memory', COUNT(*) FILTER (WHERE pos_y >= 900)
  )
  INTO stats.nodes_by_domain
  FROM nodes
  WHERE tenant_id = p_tenant_id AND is_active = TRUE;
  
  -- Update or insert stats
  INSERT INTO graph_stats (
    tenant_id, total_nodes, total_links,
    nodes_by_type, links_by_type, nodes_by_domain, computed_at
  )
  SELECT
    p_tenant_id,
    (stats.nodes_by_type->>'user')::INT + (stats.nodes_by_type->>'device')::INT +
    (stats.nodes_by_type->>'skill')::INT + (stats.nodes_by_type->>'memory')::INT +
    (stats.nodes_by_type->>'transaction')::INT + (stats.nodes_by_type->>'tag')::INT,
    (SELECT COUNT(*) FROM links WHERE tenant_id = p_tenant_id AND is_active = TRUE),
    stats.nodes_by_type,
    stats.links_by_type,
    stats.nodes_by_domain,
    NOW()
  ON CONFLICT (tenant_id) DO UPDATE
  SET
    total_nodes = EXCLUDED.total_nodes,
    total_links = EXCLUDED.total_links,
    nodes_by_type = EXCLUDED.nodes_by_type,
    links_by_type = EXCLUDED.links_by_type,
    nodes_by_domain = EXCLUDED.nodes_by_domain,
    computed_at = NOW();
END;
$$ LANGUAGE plpgsql;

-- ============================================================
-- VECTOR EMBEDDING HELPER
-- ============================================================

-- Note: In production, use OpenAI API or similar for embeddings
-- This function is a placeholder for when embedding service is integrated
CREATE OR REPLACE FUNCTION generate_node_embedding(
  p_name TEXT,
  p_description TEXT,
  p_type TEXT,
  p_content JSONB DEFAULT '{}'
)
RETURNS VECTOR(1536) AS $$
BEGIN
  -- This would call an external embedding service
  -- For now, return a zero vector as placeholder
  -- In production: use supabase-py with OpenAI or local embedding model
  RETURN NULL::VECTOR(1536);
END;
$$ LANGUAGE plpgsql STABLE;
