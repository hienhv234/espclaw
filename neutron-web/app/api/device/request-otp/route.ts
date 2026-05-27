import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';
import { publishOTP } from '@/lib/mqtt';
import { parseDeviceId, parseTenantId } from '@/lib/validation';
import { rateLimit } from '@/lib/rate-limit';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

export async function POST(request: NextRequest) {
  const rl = await rateLimit(request, 'device-api');
  if (!rl.success) {
    return NextResponse.json(
      {
        error: 'Too many OTP requests. Please wait.',
        retryAfter: Math.ceil((rl.reset - Date.now()) / 1000),
      },
      {
        status: 429,
        headers: {
          'Retry-After': String(Math.ceil((rl.reset - Date.now()) / 1000)),
          'X-RateLimit-Remaining': String(rl.remaining),
        },
      }
    );
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

  const deviceIdResult = parseDeviceId(obj.device_id);
  if (!deviceIdResult.success) {
    return NextResponse.json(
      { error: 'Invalid device ID', details: deviceIdResult.errors },
      { status: 400 }
    );
  }
  const deviceId = deviceIdResult.data;

  let resolvedTenantId = '00000000-0000-0000-0000-000000000001';

  if (obj.tenant_id) {
    const tenantResult = parseTenantId(obj.tenant_id);
    if (!tenantResult.success) {
      return NextResponse.json(
        { error: 'Invalid tenant_id format', details: tenantResult.errors },
        { status: 400 }
      );
    }
    resolvedTenantId = tenantResult.data;
  }

  const supabase = createClient(supabaseUrl, supabaseKey);

  const otp = Math.floor(100000 + Math.random() * 900000).toString();
  const expiresAt = new Date(Date.now() + 5 * 60 * 1000).toISOString();

  const { error } = await supabase
    .from('devices')
    .upsert(
      {
        mac_address: deviceId,
        device_name: deviceId,
        device_type: 'esp32s3',
        pair_code: otp,
        pair_expires_at: expiresAt,
        is_paired: false,
        tenant_id: resolvedTenantId,
      },
      { onConflict: 'mac_address', ignoreDuplicates: false }
    );

  if (error) {
    console.error('[request-otp] Supabase error:', error);
    return NextResponse.json({ error: 'Database error' }, { status: 500 });
  }

  try {
    await publishOTP(deviceId, otp);
  } catch (mqttErr) {
    console.error('[request-otp] MQTT error:', mqttErr);
    return NextResponse.json(
      { error: 'Failed to send OTP to device. Please try again.' },
      { status: 502 }
    );
  }

  return NextResponse.json(
    {
      success: true,
      message: 'OTP sent to device',
      expires_in_seconds: 300,
      rateLimit: { remaining: rl.remaining, limit: rl.limit },
    },
    { headers: { 'X-RateLimit-Remaining': String(rl.remaining) } }
  );
}
