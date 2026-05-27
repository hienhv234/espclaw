/**
 * Individual Workflow API
 * GET    /api/workflows/[id]
 * PATCH  /api/workflows/[id]
 * DELETE /api/workflows/[id]
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

const StepSchema = z.object({
  id: z.number(),
  type: z.enum(['tool', 'llm', 'condition', 'wait', 'notify', 'create_node', 'update_node', 'create_link', 'http_request', 'workflow']),
  description: z.string().optional(),
  config: z.record(z.any()),
  next_on_success: z.number().nullable().optional(),
  next_on_failure: z.number().nullable().optional(),
});

const WorkflowUpdateSchema = z.object({
  name: z.string().min(1).max(100).optional(),
  description: z.string().optional(),
  icon: z.string().optional(),
  category: z.string().optional(),
  subtype: z.string().optional(),
  trigger_type: z.enum(['voice_command', 'scheduled', 'event', 'link_activated', 'llm_fallback', 'manual']).optional(),
  trigger_pattern: z.string().optional(),
  trigger_node_id: z.string().uuid().nullable().optional(),
  trigger_config: z.record(z.any()).optional(),
  steps: z.array(StepSchema).optional(),
  on_error: z.enum(['skip', 'retry', 'fallback_llm', 'notify', 'stop']).optional(),
  max_retries: z.number().int().min(0).max(10).optional(),
  output_mapping: z.array(z.record(z.any())).optional(),
  is_enabled: z.boolean().optional(),
  priority: z.number().int().min(1).max(100).optional(),
});

// ============================================================
// GET /api/workflows/[id]
// ============================================================

export async function GET(
  _request: NextRequest,
  { params }: { params: Promise<{ id: string }> }
) {
  const { id } = await params;
  const supabase = getSupabase();

  const { data, error } = await supabase
    .from('workflows')
    .select('*')
    .eq('id', id)
    .single();

  if (error || !data) {
    return NextResponse.json({ error: 'Workflow not found' }, { status: 404 });
  }

  // Get execution stats
  const { data: execStats } = await supabase
    .from('workflow_executions')
    .select('status', { count: 'exact', head: true })
    .eq('workflow_id', id);

  const successCount = Array.isArray(execStats)
    ? execStats.filter(e => e.status === 'completed').length
    : 0;
  const failureCount = Array.isArray(execStats)
    ? execStats.filter(e => e.status === 'failed').length
    : 0;

  return NextResponse.json({
    workflow: data,
    stats: {
      success_count: successCount,
      failure_count: failureCount,
    }
  });
}

// ============================================================
// PATCH /api/workflows/[id]
// ============================================================

export async function PATCH(
  request: NextRequest,
  { params }: { params: Promise<{ id: string }> }
) {
  const { id } = await params;
  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ error: 'Invalid JSON body' }, { status: 400 });
  }

  const parsed = WorkflowUpdateSchema.safeParse(body);
  if (!parsed.success) {
    return NextResponse.json(
      { error: 'Validation failed', details: parsed.error.flatten() },
      { status: 400 }
    );
  }

  const data = parsed.data;
  const supabase = getSupabase();

  // Verify workflow exists
  const { data: existing } = await supabase
    .from('workflows')
    .select('id, tenant_id, name')
    .eq('id', id)
    .single();

  if (!existing) {
    return NextResponse.json({ error: 'Workflow not found' }, { status: 404 });
  }

  // Check unique name conflict
  if (data.name && data.name !== existing.name) {
    const { data: conflict } = await supabase
      .from('workflows')
      .select('id')
      .eq('tenant_id', existing.tenant_id)
      .eq('name', data.name)
      .single();
    if (conflict) {
      return NextResponse.json({ error: `Workflow "${data.name}" already exists` }, { status: 409 });
    }
  }

  // Validate steps if provided
  if (data.steps) {
    const stepIds = data.steps.map(s => s.id).sort((a, b) => a - b);
    for (let i = 0; i < stepIds.length; i++) {
      if (stepIds[i] !== i) {
        return NextResponse.json(
          { error: `Step IDs must be sequential starting from 0` },
          { status: 400 }
        );
      }
    }
    for (const step of data.steps) {
      const maxId = data.steps.length - 1;
      if (step.next_on_success != null && step.next_on_success !== undefined && (step.next_on_success < 0 || step.next_on_success > maxId)) {
        return NextResponse.json({ error: `Step ${step.id}: next_on_success out of range` }, { status: 400 });
      }
      if (step.next_on_failure != null && step.next_on_failure !== undefined && (step.next_on_failure < 0 || step.next_on_failure > maxId)) {
        return NextResponse.json({ error: `Step ${step.id}: next_on_failure out of range` }, { status: 400 });
      }
    }
  }

  // Increment version for sync
  const updateData = {
    ...data,
    version: existing ? undefined : undefined, // Will increment via trigger
  };

  const { data: updated, error } = await supabase
    .from('workflows')
    .update({ ...updateData, updated_at: new Date().toISOString() })
    .eq('id', id)
    .select()
    .single();

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }

  // Update linked skill node name if changed
  if (data.name && data.name !== existing.name) {
    await supabase
      .from('nodes')
      .update({ name: data.name, description: data.description ?? null })
      .eq('tenant_id', existing.tenant_id)
      .eq('name', existing.name)
      .eq('type', 'skill');
  }

  return NextResponse.json({ workflow: updated });
}

// ============================================================
// DELETE /api/workflows/[id]
// ============================================================

export async function DELETE(
  _request: NextRequest,
  { params }: { params: Promise<{ id: string }> }
) {
  const { id } = await params;
  const supabase = getSupabase();

  // Get workflow info before deletion
  const { data: existing } = await supabase
    .from('workflows')
    .select('id, tenant_id, name')
    .eq('id', id)
    .single();

  if (!existing) {
    return NextResponse.json({ error: 'Workflow not found' }, { status: 404 });
  }

  // Delete workflow (cascades to executions)
  const { error } = await supabase
    .from('workflows')
    .delete()
    .eq('id', id);

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }

  // Optionally: delete linked skill node
  await supabase
    .from('nodes')
    .update({ is_active: false })
    .eq('tenant_id', existing.tenant_id)
    .eq('name', existing.name)
    .eq('type', 'skill');

  return NextResponse.json({ success: true });
}
