/**
 * Workflow CRUD API
 * POST   /api/workflows          - Create workflow
 * GET    /api/workflows          - List workflows
 * POST   /api/workflows/execute  - Trigger workflow by input (for ESP32)
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

// ============================================================
// Schemas
// ============================================================

const StepSchema = z.object({
  id: z.number(),
  type: z.enum(['tool', 'llm', 'condition', 'wait', 'notify', 'create_node', 'update_node', 'create_link', 'http_request', 'workflow']),
  description: z.string().optional(),
  config: z.record(z.any()),
  next_on_success: z.number().nullable().optional(),
  next_on_failure: z.number().nullable().optional(),
});

const WorkflowCreateSchema = z.object({
  tenant_id: z.string().uuid(),
  device_id: z.string().uuid().optional(),
  name: z.string().min(1).max(100),
  description: z.string().optional(),
  icon: z.string().optional(),
  category: z.string().optional(),
  subtype: z.string().optional(),
  trigger_type: z.enum(['voice_command', 'scheduled', 'event', 'link_activated', 'llm_fallback', 'manual']),
  trigger_pattern: z.string().optional(),
  trigger_node_id: z.string().uuid().optional(),
  trigger_config: z.record(z.any()).optional(),
  steps: z.array(StepSchema),
  on_error: z.enum(['skip', 'retry', 'fallback_llm', 'notify', 'stop']).optional(),
  max_retries: z.number().int().min(0).max(10).optional(),
  output_mapping: z.array(z.record(z.any())).optional(),
  is_enabled: z.boolean().optional(),
  priority: z.number().int().min(1).max(100).optional(),
  created_by: z.string().optional(),
});

const WorkflowTriggerSchema = z.object({
  tenant_id: z.string().uuid(),
  device_id: z.string().uuid().optional(),
  input: z.string().optional(),
  trigger_node_id: z.string().uuid().optional(),
  session_id: z.string().uuid().optional(),
  dry_run: z.boolean().optional(),
});

const WorkflowListSchema = z.object({
  tenant_id: z.string().uuid(),
  category: z.string().optional(),
  trigger_type: z.string().optional(),
  is_enabled: z.boolean().optional(),
  limit: z.coerce.number().int().min(1).max(200).optional().default(50),
  offset: z.coerce.number().int().min(0).optional().default(0),
});

// ============================================================
// GET /api/workflows
// ============================================================

export async function GET(request: NextRequest) {
  const tenantId = request.nextUrl.searchParams.get('tenant_id');
  const category = request.nextUrl.searchParams.get('category');
  const triggerType = request.nextUrl.searchParams.get('trigger_type');
  const limit = parseInt(request.nextUrl.searchParams.get('limit') ?? '50');
  const offset = parseInt(request.nextUrl.searchParams.get('offset') ?? '0');

  if (!tenantId) {
    return NextResponse.json({ error: 'tenant_id required' }, { status: 400 });
  }

  const supabase = getSupabase();

  let query = supabase
    .from('workflows')
    .select('*')
    .eq('tenant_id', tenantId)
    .order('priority', { ascending: false })
    .range(offset, offset + limit - 1);

  if (category) query = query.eq('category', category);
  if (triggerType) query = query.eq('trigger_type', triggerType);

  const { data, error, count } = await query;

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }

  return NextResponse.json({
    workflows: data ?? [],
    total: count ?? 0,
    limit,
    offset,
  });
}

// ============================================================
// POST /api/workflows
// Create a new workflow
// ============================================================

export async function POST(request: NextRequest) {
  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ error: 'Invalid JSON body' }, { status: 400 });
  }

  const parsed = WorkflowCreateSchema.safeParse(body);
  if (!parsed.success) {
    return NextResponse.json(
      { error: 'Validation failed', details: parsed.error.flatten() },
      { status: 400 }
    );
  }

  const data = parsed.data;
  const supabase = getSupabase();

  // Check if workflow with same name exists
  const { data: existing } = await supabase
    .from('workflows')
    .select('id')
    .eq('tenant_id', data.tenant_id)
    .eq('name', data.name)
    .single();

  if (existing) {
    return NextResponse.json(
      { error: `Workflow with name "${data.name}" already exists` },
      { status: 409 }
    );
  }

  // Validate step IDs are sequential
  const stepIds = data.steps.map(s => s.id).sort((a, b) => a - b);
  for (let i = 0; i < stepIds.length; i++) {
    if (stepIds[i] !== i) {
      return NextResponse.json(
        { error: `Step IDs must be sequential starting from 0. Got: ${stepIds.join(', ')}` },
        { status: 400 }
      );
    }
  }

  // Validate next_on_success/failure step IDs exist
  for (const step of data.steps) {
    const maxId = data.steps.length - 1;
    if (step.next_on_success !== null && step.next_on_success !== undefined && (step.next_on_success < 0 || step.next_on_success > maxId)) {
      return NextResponse.json({ error: `Step ${step.id}: next_on_success ${step.next_on_success} out of range` }, { status: 400 });
    }
    if (step.next_on_failure !== null && step.next_on_failure !== undefined && (step.next_on_failure < 0 || step.next_on_failure > maxId)) {
      return NextResponse.json({ error: `Step ${step.id}: next_on_failure ${step.next_on_failure} out of range` }, { status: 400 });
    }
  }

  // Also create a linked node if this is a skill workflow
  let linkedNodeId: string | undefined;
  if (data.category === 'skill' || data.trigger_type === 'voice_command') {
    const { data: nodeData, error: nodeError } = await supabase
      .from('nodes')
      .insert({
        tenant_id: data.tenant_id,
        type: 'skill',
        subtype: data.category ?? 'general',
        name: data.name,
        description: data.description ?? null,
        content: {
          workflow_id: null, // Will update after workflow insert
          trigger_pattern: data.trigger_pattern,
          trigger_type: data.trigger_type,
        },
        is_active: true,
      })
      .select('id')
      .single();

    if (!nodeError && nodeData) {
      linkedNodeId = nodeData.id;
    }
  }

  // Insert workflow
  const insertData: Record<string, unknown> = {
    tenant_id: data.tenant_id,
    name: data.name,
    description: data.description ?? null,
    icon: data.icon ?? '⚡',
    category: data.category ?? 'general',
    subtype: data.subtype ?? null,
    trigger_type: data.trigger_type,
    trigger_pattern: data.trigger_pattern ?? null,
    trigger_node_id: data.trigger_node_id ?? null,
    trigger_config: data.trigger_config ?? {},
    steps: data.steps,
    on_error: data.on_error ?? 'fallback_llm',
    max_retries: data.max_retries ?? 3,
    output_mapping: data.output_mapping ?? [],
    is_enabled: data.is_enabled ?? true,
    priority: data.priority ?? 50,
    created_by: data.created_by ?? 'user',
    source_device: data.device_id ?? null,
  };

  const { data: workflow, error: workflowError } = await supabase
    .from('workflows')
    .insert(insertData)
    .select()
    .single();

  if (workflowError) {
    // Cleanup node if workflow insert failed
    if (linkedNodeId) {
      await supabase.from('nodes').delete().eq('id', linkedNodeId);
    }
    return NextResponse.json({ error: workflowError.message }, { status: 500 });
  }

  // Update linked node with workflow_id
  if (linkedNodeId && workflow) {
    await supabase
      .from('nodes')
      .update({ content: { workflow_id: workflow.id, trigger_pattern: data.trigger_pattern, trigger_type: data.trigger_type } })
      .eq('id', linkedNodeId);

    // Create link: trigger_node → workflow_node (if trigger_node specified)
    if (data.trigger_node_id) {
      await supabase.from('links').insert({
        tenant_id: data.tenant_id,
        source_node_id: data.trigger_node_id,
        target_node_id: linkedNodeId,
        type: 'triggers',
        weight: 0.8,
      }).catch(() => {});
    }
  }

  // Record pulse
  await supabase.from('pulses').insert({
    tenant_id: data.tenant_id,
    device_id: data.device_id ?? null,
    source_node_id: linkedNodeId ?? null,
    type: 'action',
    text: `Workflow created: ${data.name}`,
    metadata: { action: 'workflow_created', workflow_id: workflow.id, category: data.category },
  }).catch(() => {});

  return NextResponse.json({ workflow }, { status: 201 });
}
