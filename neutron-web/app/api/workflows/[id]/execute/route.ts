/**
 * Workflow Execute API
 * POST /api/workflows/[id]/execute
 *
 * Executes a workflow and returns the result.
 * This is the core workflow engine entry point.
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

const ExecuteSchema = z.object({
  tenant_id: z.string().uuid(),
  device_id: z.string().uuid().optional(),
  input: z.string().optional(),
  session_id: z.string().uuid().optional(),
  dry_run: z.boolean().optional().default(false),
  // For condition evaluation
  context: z.record(z.any()).optional().default({}),
});

type WorkflowStep = {
  id: number;
  type: string;
  description?: string;
  config: Record<string, unknown>;
  next_on_success?: number | null;
  next_on_failure?: number | null;
};

type ExecutionResult = {
  step_id: number;
  step_type: string;
  step_name: string;
  result: unknown;
  ms: number;
  success: boolean;
};

// ============================================================
// Tool registry (maps tool names to actual ESP32 tool names)
// ============================================================

const TOOL_MAP: Record<string, string> = {
  gpio_write: 'gpio_write',
  gpio_read: 'gpio_read',
  delay: 'delay',
  memory_set: 'memory_set',
  memory_get: 'memory_get',
  wifi_scan: 'wifi_scan',
  cron_schedule: 'cron_schedule',
  notify_esp: 'notify_esp',
};

function simulateToolCall(toolName: string, config: Record<string, unknown>): { success: boolean; output: string } {
  const tool = TOOL_MAP[toolName];
  if (!tool) {
    return { success: false, output: `Unknown tool: ${toolName}` };
  }

  // Simulate tool execution
  switch (tool) {
    case 'gpio_write':
      return { success: true, output: `GPIO ${config.pin} set to ${config.state}` };
    case 'gpio_read':
      return { success: true, output: `GPIO ${config.pin} = 1` };
    case 'delay':
      return { success: true, output: `Waited ${config.ms}ms` };
    case 'memory_set':
      return { success: true, output: `Set ${config.key} = ${config.value}` };
    case 'memory_get':
      return { success: true, output: `Got ${config.key} = ${config.value ?? 'null'}` };
    case 'wifi_scan':
      return { success: true, output: 'Found 3 networks' };
    case 'cron_schedule':
      return { success: true, output: `Cron job "${config.name}" scheduled` };
    case 'notify_esp':
      return { success: true, output: `Notification sent: ${config.message}` };
    default:
      return { success: true, output: `${tool} executed` };
  }
}

// ============================================================
// LLM simulation (replace with real LLM call)
// ============================================================

async function simulateLLMCall(config: Record<string, unknown>, input: string): Promise<string> {
  // In production: call Anthropic/OpenAI here
  const prompt = config.prompt as string || 'Analyze this input';
  const template = prompt.replace('{{input}}', input);
  return `[LLM] ${template.substring(0, 80)}...`;
}

// ============================================================
// Core workflow execution engine
// ============================================================

async function executeWorkflow(
  workflow: Record<string, unknown>,
  input: string,
  dryRun: boolean,
  context: Record<string, unknown>
): Promise<{ success: boolean; results: ExecutionResult[]; error?: string; output?: string }> {
  const steps = (workflow.steps as WorkflowStep[]) || [];
  if (steps.length === 0) {
    return { success: true, results: [], output: 'No steps to execute' };
  }

  const results: ExecutionResult[] = [];
  let currentStepId = 0;
  let stepOutputs: Record<number, unknown> = {};
  let output: string = '';
  let iterations = 0;
  const MAX_ITERATIONS = steps.length * 2; // Prevent infinite loops

  while (currentStepId < steps.length && iterations < MAX_ITERATIONS) {
    iterations++;
    const step = steps.find(s => s.id === currentStepId);
    if (!step) break;

    const start = Date.now();
    let stepSuccess = true;
    let stepResult: unknown = null;

    try {
      switch (step.type) {
        case 'tool': {
          const toolResult = simulateToolCall(step.config.tool as string || '', step.config);
          stepSuccess = toolResult.success;
          stepResult = toolResult.output;
          output += `[Tool] ${stepResult}\n`;
          break;
        }

        case 'llm': {
          const llmResult = await simulateLLMCall(step.config, input);
          stepResult = llmResult;
          output += `[LLM] ${llmResult}\n`;
          break;
        }

        case 'condition': {
          // Evaluate condition
          const field = step.config.field as string;
          const operator = step.config.operator as string;
          const value = step.config.value;
          const actual = context[field] ?? stepOutputs[currentStepId - 1];

          let conditionMet = false;
          switch (operator) {
            case 'eq': conditionMet = actual === value; break;
            case 'ne': conditionMet = actual !== value; break;
            case 'gt': conditionMet = Number(actual) > Number(value); break;
            case 'lt': conditionMet = Number(actual) < Number(value); break;
            case 'contains': conditionMet = String(actual).includes(String(value)); break;
            case 'matches': conditionMet = new RegExp(value as string).test(String(actual)); break;
            default: conditionMet = false;
          }

          stepResult = { condition_met: conditionMet, actual, expected: value };
          stepSuccess = conditionMet;
          output += `[Condition] ${field} ${operator} ${value} → ${conditionMet ? 'TRUE' : 'FALSE'}\n`;
          break;
        }

        case 'wait': {
          const ms = step.config.ms as number || 1000;
          if (!dryRun) {
            await new Promise(resolve => setTimeout(resolve, Math.min(ms, 100))); // Cap wait in dry-run
          }
          stepResult = `Waited ${ms}ms`;
          output += `[Wait] ${stepResult}\n`;
          break;
        }

        case 'notify': {
          const msg = step.config.message as string;
          const channel = step.config.channel as string || 'esp_voice';
          stepResult = { message: msg, channel, sent: !dryRun };
          output += `[Notify] ${channel}: ${msg}\n`;
          break;
        }

        case 'create_node': {
          const nodeType = step.config.node_type as string || 'memory';
          const name = typeof step.config.name_template === 'string'
            ? step.config.name_template.replace('{{input}}', input)
            : input;
          stepResult = { node_type: nodeType, name, created: !dryRun };
          output += `[CreateNode] ${nodeType}: ${name}\n`;
          break;
        }

        case 'update_node': {
          const nodeId = step.config.node_id as string;
          const field = step.config.field as string;
          const value = step.config.value;
          stepResult = { node_id: nodeId, field, value, updated: !dryRun };
          output += `[UpdateNode] ${nodeId}.${field} = ${value}\n`;
          break;
        }

        case 'create_link': {
          const source = step.config.source_node_id as string;
          const target = step.config.target_node_id as string;
          const linkType = step.config.link_type as string || 'related_to';
          stepResult = { source, target, type: linkType, created: !dryRun };
          output += `[CreateLink] ${source} --[${linkType}]--> ${target}\n`;
          break;
        }

        case 'http_request': {
          const url = step.config.url as string;
          const method = step.config.method as string || 'GET';
          stepResult = { url, method, status: dryRun ? 'simulated' : 200 };
          output += `[HTTP] ${method} ${url} → ${stepResult.status}\n`;
          break;
        }

        case 'workflow': {
          const subWorkflowId = step.config.workflow_id as string;
          stepResult = { sub_workflow_id: subWorkflowId, executed: !dryRun };
          output += `[SubWorkflow] ${subWorkflowId}\n`;
          break;
        }

        default:
          stepResult = `Unknown step type: ${step.type}`;
          stepSuccess = false;
      }
    } catch (err) {
      stepSuccess = false;
      stepResult = String(err);
      output += `[Error] ${err}\n`;
    }

    stepOutputs[currentStepId] = stepResult;

    results.push({
      step_id: step.id,
      step_type: step.type,
      step_name: step.description || step.type,
      result: stepResult,
      ms: Date.now() - start,
      success: stepSuccess,
    });

    // Determine next step
    if (stepSuccess) {
      currentStepId = step.next_on_success !== null && step.next_on_success !== undefined
        ? step.next_on_success
        : step.id + 1;
    } else {
      const onError = workflow.on_error as string || 'fallback_llm';
      if (onError === 'stop') {
        break;
      } else if (onError === 'skip') {
        currentStepId = step.id + 1;
      } else {
        // fallback_llm or retry: go to next or stop
        currentStepId = step.next_on_failure !== null && step.next_on_failure !== undefined
          ? step.next_on_failure
          : steps.length; // End
      }
    }
  }

  const lastResult = results[results.length - 1];
  return {
    success: lastResult?.success ?? true,
    results,
    output: output.trim(),
    error: !lastResult?.success ? String(lastResult?.result) : undefined,
  };
}

// ============================================================
// POST /api/workflows/[id]/execute
// ============================================================

export async function POST(
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

  const parsed = ExecuteSchema.safeParse(body);
  if (!parsed.success) {
    return NextResponse.json(
      { error: 'Validation failed', details: parsed.error.flatten() },
      { status: 400 }
    );
  }

  const { tenant_id, device_id, input, session_id, dry_run, context } = parsed.data;
  const supabase = getSupabase();

  // Fetch workflow
  const { data: workflow, error } = await supabase
    .from('workflows')
    .select('*')
    .eq('id', id)
    .single();

  if (error || !workflow) {
    return NextResponse.json({ error: 'Workflow not found' }, { status: 404 });
  }

  if (!workflow.is_enabled) {
    return NextResponse.json({ error: 'Workflow is disabled' }, { status: 400 });
  }

  // Check trigger pattern match for voice_command
  if (workflow.trigger_type === 'voice_command' && workflow.trigger_pattern) {
    const pattern = workflow.trigger_pattern as string;
    const inputText = input || '';
    let matched = false;

    if (pattern.startsWith('^')) {
      matched = new RegExp(pattern, 'i').test(inputText);
    } else {
      matched = inputText.toLowerCase().includes(pattern.toLowerCase());
    }

    if (!matched && !dry_run) {
      return NextResponse.json({
        success: false,
        matched: false,
        error: `Input does not match trigger pattern: ${pattern}`,
        workflow: { id: workflow.id, name: workflow.name },
      }, { status: 200 }); // 200 because it's not an error
    }
  }

  if (dry_run) {
    // Dry run: just validate and return
    return NextResponse.json({
      success: true,
      dry_run: true,
      matched: true,
      workflow: {
        id: workflow.id,
        name: workflow.name,
        steps_count: (workflow.steps as unknown[])?.length ?? 0,
      },
    });
  }

  // Record execution start
  const execStartTime = Date.now();
  const { data: execRecord } = await supabase.rpc('workflow_execution_start', {
    p_workflow_id: id,
    p_tenant_id: tenant_id,
    p_device_id: device_id ?? null,
    p_trigger_type: workflow.trigger_type,
    p_trigger_input: { input, context },
    p_trigger_node_id: workflow.trigger_node_id ?? null,
    p_session_id: session_id ?? null,
  }).catch(() => ({ data: null }));

  const executionId = (execRecord as { id?: string })?.id;

  // Execute workflow
  const execResult = await executeWorkflow(workflow, input || '', dry_run, context);

  const durationMs = Date.now() - execStartTime;
  const stepsExecuted = execResult.results.map(r => ({
    step: r.step_id,
    type: r.step_type,
    name: r.step_name,
    result: r.result,
    ms: r.ms,
    success: r.success,
  }));

  // Record execution end
  if (executionId) {
    await supabase.rpc('workflow_execution_end', {
      p_execution_id: executionId,
      p_status: execResult.success ? 'completed' : 'failed',
      p_steps_executed: stepsExecuted,
      p_result_data: { output: execResult.output, input },
      p_error_message: execResult.error ?? null,
      p_error_step: execResult.results[execResult.results.length - 1]?.success === false
        ? execResult.results[execResult.results.length - 1]?.step_id ?? null
        : null,
      p_duration_ms: durationMs,
    }).catch(() => {});
  }

  // Create pulse
  await supabase.from('pulses').insert({
    tenant_id,
    device_id: device_id ?? null,
    source_node_id: workflow.trigger_node_id ?? null,
    type: 'action',
    text: `Workflow executed: ${workflow.name}`,
    metadata: {
      workflow_id: id,
      execution_id: executionId,
      input,
      steps_executed: stepsExecuted.length,
      success: execResult.success,
      duration_ms: durationMs,
    },
  }).catch(() => {});

  return NextResponse.json({
    success: execResult.success,
    matched: true,
    workflow: {
      id: workflow.id,
      name: workflow.name,
      icon: workflow.icon,
    },
    execution_id: executionId,
    duration_ms: durationMs,
    results: stepsExecuted,
    output: execResult.output,
    error: execResult.error,
  });
}
