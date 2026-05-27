import { createClient } from '@supabase/supabase-js';
import { NextRequest, NextResponse } from 'next/server';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

// GET /api/reminders?device_id=xxx&window=5
// Returns reminders due within window (default 5 minutes)
export async function GET(request: NextRequest) {
  const deviceId = request.nextUrl.searchParams.get('device_id');
  const tenantId = request.nextUrl.searchParams.get('tenant_id');
  const windowMin = parseInt(request.nextUrl.searchParams.get('window') ?? '5');

  if (!tenantId) {
    return NextResponse.json({ error: 'tenant_id required' }, { status: 400 });
  }

  const supabase = createClient(supabaseUrl, supabaseKey);

  // Use the RPC function for efficient due-reminder query
  const { data, error } = await supabase.rpc('get_due_reminders', {
    p_device_id: deviceId,
    p_window_minutes: windowMin
  });

  if (error) {
    // Fallback to direct query if RPC fails
    const now = new Date();
    const windowEnd = new Date(now.getTime() + windowMin * 60 * 1000);
    const { data: fallback, error: fallbackError } = await supabase
      .from('reminders')
      .select('*, appointments(title, description, notify_channel)')
      .eq('tenant_id', tenantId)
      .eq('status', 'pending')
      .gte('trigger_at', now.toISOString())
      .lte('trigger_at', windowEnd.toISOString())
      .order('trigger_at');

    if (fallbackError) {
      return NextResponse.json({ error: fallbackError.message }, { status: 500 });
    }
    return NextResponse.json({ reminders: fallback ?? [] });
  }

  if (error) return NextResponse.json({ error: error.message }, { status: 500 });
  return NextResponse.json({ reminders: data ?? [] });
}

// POST /api/reminders/acknowledge
// Body: { reminder_id }
export async function POST(request: NextRequest) {
  const body = await request.json().catch(() => null);
  const { reminder_id } = body ?? {};

  if (!reminder_id) {
    return NextResponse.json({ error: 'reminder_id required' }, { status: 400 });
  }

  const supabase = createClient(supabaseUrl, supabaseKey);
  const result = await supabase.rpc('acknowledge_reminder', { p_reminder_id: reminder_id });

  if (result.error) return NextResponse.json({ error: result.error.message }, { status: 500 });
  return NextResponse.json(result.data);
}
