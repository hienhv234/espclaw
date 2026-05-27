import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

export async function POST(request: NextRequest) {
  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ error: 'Invalid JSON' }, { status: 400 });
  }

  const obj = body as Record<string, unknown>;
  const deviceId = typeof obj.device_id === 'string' ? obj.device_id.toUpperCase() : null;

  if (!deviceId) {
    return NextResponse.json({ error: 'device_id required' }, { status: 400 });
  }

  const supabase = createClient(supabaseUrl, supabaseKey);

  const { error } = await supabase
    .from('devices')
    .update({
      is_online: true,
      last_seen_at: new Date().toISOString(),
    })
    .eq('mac_address', deviceId);

  if (error) {
    return NextResponse.json({ error: 'DB update failed', details: error.message }, { status: 500 });
  }

  return NextResponse.json({ ok: true, device_id: deviceId, ts: new Date().toISOString() });
}
