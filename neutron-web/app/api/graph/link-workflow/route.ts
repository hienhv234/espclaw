/**
 * Link Workflow to Node API
 * POST /api/graph/link-workflow
 *
 * Links a workflow to an input node (e.g., voice → workflow)
 */

import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';
import { z } from 'zod';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

function getSupabase() {
  return createClient(supabaseUrl, supabaseKey, {
    auth: { autoRefreshToken: false, persistSession: false }
  });
}

const LinkSchema = z.object({
  tenant_id: z.string().uuid(),
  workflow_id: z.string().uuid(),
  source_node_id: z.string().uuid().optional(),   // trigger node
  link_type: z.enum(['triggers', 'enabled_by', 'related_to', 'owns']).optional().default('triggers'),
  weight: z.number().min(0).max(1).optional().default(0.8),
});

export async function POST(request: NextRequest) {
  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 });
  }

  const parsed = LinkSchema.safeParse(body);
  if (!parsed.success) {
    return NextResponse.json(
      { error: 'Validation failed', details: parsed.error.flatten() },
      { status: 400 }
    );
  }

  const { tenant_id, workflow_id, source_node_id, link_type, weight } = parsed.data;
  const supabase = getSupabase();

  // Verify workflow exists
  const { data: workflow } = await supabase
    .from('workflows')
    .select('id, name, trigger_node_id')
    .eq('id', workflow_id)
    .single();

  if (!workflow) {
    return NextResponse.json({ error: 'Workflow not found' }, { status: 404 });
  }

  // Determine source node (explicit or from workflow's trigger_node)
  const triggerNodeId = source_node_id || workflow.trigger_node_id;
  if (!triggerNodeId) {
    return NextResponse.json(
      { error: 'No source node provided and workflow has no trigger_node_id set' },
      { status: 400 }
    );
  }

  // Create link
  const { data: link, error } = await supabase
    .from('links')
    .upsert({
      tenant_id,
      source_node_id: triggerNodeId,
      target_node_id: workflow_id,  // workflow nodes use their own id
      type: link_type ?? 'triggers',
      weight: weight ?? 0.8,
      is_active: true,
    }, {
      onConflict: 'tenant_id,source_node_id,target_node_id,type',
    })
    .select()
    .single();

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }

  // Update workflow's trigger_node_id if not set
  if (!workflow.trigger_node_id) {
    await supabase
      .from('workflows')
      .update({ trigger_node_id: triggerNodeId })
      .eq('id', workflow_id);
  }

  return NextResponse.json({ link, success: true });
}
