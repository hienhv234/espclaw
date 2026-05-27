/**
 * Graph Workflow API
 * GET /api/graph/workflow-tree       - Get full workflow tree for 3D viz
 * GET /api/graph/node-workflows    - Get workflows linked to a node
 * POST /api/graph/link-workflow    - Link a workflow to a node
 * GET /api/graph/stats             - Dashboard stats
 */

import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

function getSupabase() {
  return createClient(supabaseUrl, supabaseKey, {
    auth: { autoRefreshToken: false, persistSession: false }
  });
}

// GET /api/graph/workflow-tree
export async function GET(request: NextRequest) {
  const tenantId = request.nextUrl.searchParams.get('tenant_id');
  const type = request.nextUrl.searchParams.get('type') || 'tree';
  const nodeId = request.nextUrl.searchParams.get('node_id');

  if (!tenantId) {
    return NextResponse.json({ error: 'tenant_id required' }, { status: 400 });
  }

  const supabase = getSupabase();

  // Return node-workflows if node_id provided
  if (nodeId) {
    const { data: linkedNodes } = await supabase
      .from('nodes')
      .select('id, name, type, subtype, content, pos_x, pos_y, pos_z, executable')
      .eq('tenant_id', tenantId)
      .eq('type', 'skill')
      .eq('is_active', true)
      .limit(50);

    const nodeIds = (linkedNodes || []).map(n => n.id);

    if (nodeIds.length === 0) {
      return NextResponse.json({ workflows: [], nodes: [] });
    }

    const { data: workflows } = await supabase
      .from('workflows')
      .select('*')
      .eq('tenant_id', tenantId)
      .in('trigger_node_id', nodeIds)
      .eq('is_enabled', true);

    return NextResponse.json({
      nodes: linkedNodes || [],
      workflows: workflows || [],
    });
  }

  // Return full workflow tree
  if (type === 'tree') {
    const [nodesResult, workflowsResult, linksResult] = await Promise.all([
      supabase
        .from('nodes')
        .select('id, name, type, subtype, content, pos_x, pos_y, pos_z, is_active, pulse_strength')
        .eq('tenant_id', tenantId)
        .eq('is_active', true)
        .limit(200),
      supabase
        .from('workflows')
        .select('id, name, icon, category, trigger_type, trigger_pattern, trigger_node_id, priority, is_enabled, trigger_count, success_count')
        .eq('tenant_id', tenantId)
        .eq('is_enabled', true)
        .order('priority', { ascending: false })
        .limit(100),
      supabase
        .from('links')
        .select('id, source_node_id, target_node_id, type, weight')
        .eq('tenant_id', tenantId)
        .eq('is_active', true)
        .limit(500),
    ]);

    // Get recent executions
    const { data: recentExecutions } = await supabase
      .from('workflow_executions')
      .select('workflow_id, status, started_at, duration_ms')
      .eq('tenant_id', tenantId)
      .order('started_at', { ascending: false })
      .limit(50);

    const workflowMap = new Map((workflowsResult.data || []).map(w => [w.id, w]));
    const execMap = new Map<string, number>();
    for (const e of (recentExecutions || [])) {
      if (!execMap.has(e.workflow_id)) {
        execMap.set(e.workflow_id, execMap.size);
      }
    }

    // Enrich workflows with stats
    const enrichedWorkflows = (workflowsResult.data || []).map(w => ({
      ...w,
      is_active: execMap.has(w.id),
      last_executed_at: recentExecutions?.find(e => e.workflow_id === w.id)?.started_at,
    }));

    return NextResponse.json({
      nodes: nodesResult.data || [],
      workflows: enrichedWorkflows,
      links: linksResult.data || [],
      stats: {
        total_nodes: nodesResult.data?.length ?? 0,
        total_workflows: workflowsResult.data?.length ?? 0,
        total_links: linksResult.data?.length ?? 0,
      },
    });
  }

  return NextResponse.json({ error: 'Unknown type' }, { status: 400 });
}
