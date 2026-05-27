import { createClient } from '@supabase/supabase-js';
import { NextRequest, NextResponse } from 'next/server';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

const sb = () => createClient(supabaseUrl, supabaseKey);

// GET /api/appointments?tenant_id=xxx&status=pending
export async function GET(request: NextRequest) {
  const tenantId = request.nextUrl.searchParams.get('tenant_id');
  const status = request.nextUrl.searchParams.get('status') ?? 'pending';
  const limit = parseInt(request.nextUrl.searchParams.get('limit') ?? '50');

  if (!tenantId) {
    return NextResponse.json({ error: 'tenant_id required' }, { status: 400 });
  }

  const supabase = sb();
  let query = supabase
    .from('appointments')
    .select('*, reminders(id, trigger_at, status)')
    .eq('tenant_id', tenantId)
    .order('scheduled_at');

  if (status !== 'all') {
    query = query.eq('status', status);
  }

  query = query.limit(limit);

  const { data, error } = await query;
  if (error) return NextResponse.json({ error: error.message }, { status: 500 });

  return NextResponse.json({ appointments: data ?? [] });
}

// POST /api/appointments
// Body: { tenant_id, device_id?, title, description?, scheduled_at, timezone?, notify_channel?, source_type? }
export async function POST(request: NextRequest) {
  const body = await request.json().catch(() => null);
  const {
    tenant_id, device_id, title, description, scheduled_at,
    timezone, notify_channel, source_type, linked_entity_id
  } = body ?? {};

  if (!tenant_id || !title || !scheduled_at) {
    return NextResponse.json(
      { error: 'tenant_id, title, scheduled_at required' },
      { status: 400 }
    );
  }

  const supabase = sb();

  // Insert appointment
  const { data: appt, error: apptError } = await supabase
    .from('appointments')
    .insert({
      tenant_id, device_id, title, description, scheduled_at,
      timezone: timezone ?? 'Asia/Ho_Chi_Minh',
      notify_channel: notify_channel ?? 'esp_voice',
      source_type: source_type ?? 'web',
      linked_entity_id
    })
    .select()
    .single();

  if (apptError) {
    return NextResponse.json({ error: apptError.message }, { status: 500 });
  }

  // Auto-create a reminder at appointment time
  const { data: reminder, error: reminderError } = await supabase
    .from('reminders')
    .insert({
      tenant_id,
      appointment_id: appt.id,
      device_id,
      trigger_at: scheduled_at,
      offset_minutes: 0,
    })
    .select()
    .single();

  if (reminderError) {
    // Non-fatal — appointment created, reminder failed
    console.warn('Failed to create reminder:', reminderError.message);
  }

  // Optionally create knowledge node for this appointment
  if (title) {
    await supabase.rpc('create_appointment_node', {
      p_tenant_id: tenant_id,
      p_appointment_id: appt.id,
      p_title: title
    }).catch(() => {});
  }

  return NextResponse.json({ appointment: appt, reminder: reminder ?? null }, { status: 201 });
}

// PATCH /api/appointments?id=xxx
export async function PATCH(request: NextRequest) {
  const id = request.nextUrl.searchParams.get('id');
  if (!id) return NextResponse.json({ error: 'id required' }, { status: 400 });

  const body = await request.json().catch(() => null);
  if (!body || Object.keys(body).length === 0) {
    return NextResponse.json({ error: 'body required' }, { status: 400 });
  }

  const supabase = sb();
  const { data, error } = await supabase
    .from('appointments')
    .update({ ...body, updated_at: new Date().toISOString() })
    .eq('id', id)
    .select()
    .single();

  if (error) return NextResponse.json({ error: error.message }, { status: 500 });
  return NextResponse.json({ appointment: data });
}

// DELETE /api/appointments?id=xxx
export async function DELETE(request: NextRequest) {
  const id = request.nextUrl.searchParams.get('id');
  if (!id) return NextResponse.json({ error: 'id required' }, { status: 400 });

  const supabase = sb();
  const { error } = await supabase
    .from('appointments')
    .update({ status: 'cancelled', updated_at: new Date().toISOString() })
    .eq('id', id);

  if (error) return NextResponse.json({ error: error.message }, { status: 500 });

  // Cancel associated reminders
  await supabase
    .from('reminders')
    .update({ status: 'cancelled', updated_at: new Date().toISOString() })
    .eq('appointment_id', id);

  return NextResponse.json({ ok: true });
}
