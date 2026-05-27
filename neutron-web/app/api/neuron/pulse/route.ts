import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';
import { parseTenantId } from '@/lib/validation';
import { rateLimit } from '@/lib/rate-limit';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

const PULSE_TYPES = new Set(['perception', 'thought', 'action', 'system', 'ai_insight']);

export async function POST(request: NextRequest) {
  const rl = await rateLimit(request, 'neuron-api');
  if (!rl.success) {
    return NextResponse.json({ error: 'Rate limit exceeded' }, { status: 429 });
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ error: 'Invalid JSON body' }, { status: 400 });
  }
  if (!body || typeof body !== 'object') {
    return NextResponse.json({ error: 'Request body must be a JSON object' }, { status: 400 });
  }
  const obj = body as Record<string, unknown>;

  if (!obj.tenant_id) {
    return NextResponse.json({ error: 'tenant_id is required' }, { status: 400 });
  }
  const tenantResult = parseTenantId(obj.tenant_id);
  if (!tenantResult.success) {
    return NextResponse.json({ error: 'Invalid tenant_id format', details: tenantResult.errors }, { status: 400 });
  }
  const tenantId = tenantResult.data;

  const pulseType = typeof obj.type === 'string' ? obj.type.trim() : 'action';
  if (!PULSE_TYPES.has(pulseType)) {
    return NextResponse.json(
      { error: 'type must be one of: perception, thought, action, system, ai_insight' },
      { status: 400 }
    );
  }

  const supabase = createClient(supabaseUrl, supabaseKey);

  const { data: pulse, error } = await supabase
    .from('pulses')
    .insert({
      tenant_id: tenantId,
      type: pulseType,
      source_node_id: (obj.source_node_id as string) || null,
      target_node_id: (obj.target_node_id as string) || null,
      link_id: (obj.link_id as string) || null,
      text: (obj.text as string) || null,
      intensity: typeof obj.intensity === 'number' ? obj.intensity : 0.5,
      metadata: (obj.metadata as Record<string, unknown>) || {},
    })
    .select()
    .single();

  if (error) {
    console.error('[neuron/pulse] Insert error:', error);
    return NextResponse.json({ error: 'Failed to create pulse' }, { status: 500 });
  }

  if (obj.target_node_id) {
    await supabase
      .from('nodes')
      .update({ last_pulse_at: new Date().toISOString() })
      .eq('id', obj.target_node_id);
  }

  triggerPatternDetection(tenantId).catch((err: unknown) =>
    console.warn('[neuron/pulse] Pattern detection trigger failed:', err)
  );

  return NextResponse.json({ success: true, pulse }, { headers: { 'X-RateLimit-Remaining': String(rl.remaining) } });
}

async function triggerPatternDetection(tenantId: string) {
  const serviceKey = process.env.SUPABASE_SERVICE_ROLE_KEY;
  if (!serviceKey) return;
  const baseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL;
  if (!baseUrl) return;

  await fetch(`${baseUrl}/functions/v1/detect-patterns`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Bearer ${serviceKey}`,
      apikey: serviceKey,
    },
    body: JSON.stringify({ tenant_id: tenantId, lookback_hours: 24 }),
  });
}
