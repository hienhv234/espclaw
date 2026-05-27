/**
 * Workflow Trigger API
 * POST /api/workflows/trigger
 *
 * Main entry point for ESP32. Finds matching workflow(s) by priority,
 * executes the top match, returns tool calls + response.
 * This replaces LLM decision-making with workflow-first routing.
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

const TriggerSchema = z.object({
  tenant_id: z.string().uuid(),
  device_id: z.string().uuid().optional(),
  input: z.string(),                    // Raw user input (voice/text)
  session_id: z.string().uuid().optional(),
  dry_run: z.boolean().optional().default(false),
  include_llm_fallback: z.boolean().optional().default(true),
});

type WorkflowStep = {
  id: number;
  type: string;
  tool?: string;
  params?: Record<string, unknown>;
  milliseconds?: number;
  condition?: { field: string; operator: string; value: unknown };
  next_on_success?: number | null;
  next_on_failure?: number | null;
  description?: string;
};

// ============================================================
// Pattern matching
// ============================================================

function matchPattern(pattern: string, input: string): boolean {
  if (!pattern) return false;
  if (pattern.startsWith('^')) {
    try {
      return new RegExp(pattern, 'i').test(input);
    } catch {
      return input.toLowerCase().includes(pattern.toLowerCase());
    }
  }
  // Case-insensitive contains
  return input.toLowerCase().includes(pattern.toLowerCase());
}

// ============================================================
// Tool execution (simulated on cloud, real on ESP32)
// ============================================================

function simulateToolCall(toolName: string, params: Record<string, unknown>): { success: boolean; result: string } {
  switch (toolName) {
    case 'gpio_write': return { success: true, result: `GPIO ${params.pin} → ${params.state === 1 ? 'HIGH' : 'LOW'}` };
    case 'gpio_read': return { success: true, result: `GPIO ${params.pin} = 1` };
    case 'gpio_read_all': return { success: true, result: 'GPIO states: {2:1, 4:0, 5:1}' };
    case 'get_diagnostics': return { success: true, result: 'heap: 185KB, uptime: 2h15m' };
    case 'get_version': return { success: true, result: 'ESPClaw v1.0.0' };
    case 'get_time': return { success: true, result: `Time: ${new Date().toLocaleString('vi-VN')}` };
    case 'wifi_scan': return { success: true, result: 'Networks: HomeWiFi, Office-5G, Guest' };
    case 'get_network_info': return { success: true, result: 'IP: 192.168.1.42, RSSI: -62dBm' };
    case 'memory_set': return { success: true, result: `Set ${params.key} = ${params.value}` };
    case 'memory_get': return { success: true, result: `${params.key} = stored_value` };
    case 'memory_list': return { success: true, result: 'Keys: u_brightness, u_theme, u_lang' };
    case 'memory_delete': return { success: true, result: `Deleted ${params.key}` };
    case 'set_persona': return { success: true, result: `Persona → ${params.persona}` };
    case 'get_persona': return { success: true, result: 'Persona: friendly' };
    case 'cron_schedule': return { success: true, result: `Scheduled: ${params.action}` };
    case 'cron_list': return { success: true, result: 'Cron jobs: 3 active' };
    case 'cron_cancel': return { success: true, result: `Cancelled job ${params.id}` };
    case 'cron_cancel_all': return { success: true, result: 'All cron jobs cancelled' };
    case 'set_timezone': return { success: true, result: `Timezone → ${params.timezone}` };
    case 'delay': return { success: true, result: `Waited ${params.milliseconds}ms` };
    default: return { success: false, result: `Unknown tool: ${toolName}` };
  }
}

// ============================================================
// Execute a single workflow
// ============================================================

async function executeWorkflow(
  workflow: Record<string, unknown>,
  input: string,
  dryRun: boolean
): Promise<{
  success: boolean;
  steps: Array<{ id: number; tool: string; result: string; ms: number }>;
  response: string;
}> {
  const steps = (workflow.steps as WorkflowStep[]) || [];
  const results: Array<{ id: number; tool: string; result: string; ms: number }> = [];
  let response = '';
  let conditionMet = true;

  for (const step of steps) {
    const start = Date.now();

    if (step.type === 'tool' && step.tool) {
      const r = simulateToolCall(step.tool, step.params || {});
      results.push({ id: step.id, tool: step.tool, result: r.result, ms: Date.now() - start });
      response += `${step.description || step.tool}: ${r.result}\n`;
      if (!r.success) conditionMet = false;
    }

    if (step.type === 'wait' && step.milliseconds) {
      if (!dryRun) await new Promise(resolve => setTimeout(resolve, Math.min(step.milliseconds, 100)));
      results.push({ id: step.id, tool: 'delay', result: `Waited ${step.milliseconds}ms`, ms: Date.now() - start });
    }
  }

  return { success: conditionMet, steps: results, response: response.trim() };
}

// ============================================================
// POST /api/workflows/trigger
// ============================================================

export async function POST(request: NextRequest) {
  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 });
  }

  const parsed = TriggerSchema.safeParse(body);
  if (!parsed.success) {
    return NextResponse.json(
      { error: 'Validation failed', details: parsed.error.flatten() },
      { status: 400 }
    );
  }

  const { tenant_id, device_id, input, session_id, dry_run, include_llm_fallback } = parsed.data;
  const supabase = getSupabase();

  // 1. Fetch all enabled workflows ordered by priority
  const { data: workflows, error } = await supabase
    .from('workflows')
    .select('*')
    .eq('tenant_id', tenant_id)
    .eq('is_enabled', true)
    .eq('trigger_type', 'voice_command')
    .order('priority', { ascending: false })
    .limit(20);

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }

  // 2. Find first matching workflow (by pattern)
  const matched = (workflows || []).find(w =>
    matchPattern(w.trigger_pattern || '', input)
  );

  if (!matched) {
    // No workflow match → return available workflows for LLM fallback
    const available = (workflows || []).map(w => ({
      id: w.id,
      name: w.name,
      icon: w.icon,
      pattern: w.trigger_pattern,
    }));

    return NextResponse.json({
      matched: false,
      workflows: available,
      fallback_needed: include_llm_fallback,
      message: `No workflow matched input: "${input.substring(0, 50)}..."`,
    });
  }

  if (dry_run) {
    return NextResponse.json({
      matched: true,
      workflow: {
        id: matched.id,
        name: matched.name,
        icon: matched.icon,
        pattern: matched.trigger_pattern,
        steps_count: ((matched.steps as WorkflowStep[]) || []).length,
      },
      dry_run: true,
    });
  }

  // 3. Execute the matched workflow
  const execStart = Date.now();
  const execResult = await executeWorkflow(matched, input, dry_run);
  const durationMs = Date.now() - execStart;

  // 4. Record execution in DB
  const { data: execRecord } = await supabase.rpc('workflow_execution_start', {
    p_workflow_id: matched.id,
    p_tenant_id: tenant_id,
    p_device_id: device_id ?? null,
    p_trigger_type: 'voice_command',
    p_trigger_input: { input },
    p_trigger_node_id: matched.trigger_node_id ?? null,
    p_session_id: session_id ?? null,
  }).catch(() => ({ data: null }));

  if (execRecord) {
    await supabase.rpc('workflow_execution_end', {
      p_execution_id: (execRecord as { id?: string }).id,
      p_status: execResult.success ? 'completed' : 'failed',
      p_steps_executed: execResult.steps.map(s => ({
        step: s.id,
        type: 'tool',
        name: s.tool,
        result: s.result,
        ms: s.ms,
        success: true,
      })),
      p_result_data: { response: execResult.response, input },
      p_error_message: execResult.success ? null : 'Tool execution failed',
      p_duration_ms: durationMs,
      p_llm_calls: 0,
      p_tool_calls: execResult.steps.length,
    }).catch(() => {});
  }

  // 5. Record pulse
  await supabase.from('pulses').insert({
    tenant_id,
    device_id: device_id ?? null,
    source_node_id: matched.trigger_node_id ?? null,
    type: 'action',
    text: `Workflow triggered: ${matched.name}`,
    metadata: {
      workflow_id: matched.id,
      workflow_name: matched.name,
      input: input.substring(0, 100),
      steps_executed: execResult.steps.length,
      success: execResult.success,
      duration_ms: durationMs,
    },
  }).catch(() => {});

  return NextResponse.json({
    matched: true,
    workflow: {
      id: matched.id,
      name: matched.name,
      icon: matched.icon,
      category: matched.category,
    },
    execution: {
      success: execResult.success,
      duration_ms: durationMs,
      steps: execResult.steps,
    },
    response: execResult.response,
  });
}
