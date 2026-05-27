import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';
import { publishLoginSuccess } from '@/lib/mqtt';
import { parseDeviceId, parseOTP } from '@/lib/validation';
import { rateLimit } from '@/lib/rate-limit';
import { sendTelegramMessage, escapeMarkdown } from '@/lib/telegram';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

export async function POST(request: NextRequest) {
  const rl = await rateLimit(request, 'device-api');
  if (!rl.success) {
    return NextResponse.json(
      { error: 'Too many verification attempts. Please wait.', retryAfter: Math.ceil((rl.reset - Date.now()) / 1000) },
      { status: 429 }
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

  const deviceResult = parseDeviceId(obj.device_id);
  if (!deviceResult.success) {
    return NextResponse.json({ error: 'Invalid device ID', details: deviceResult.errors }, { status: 400 });
  }
  const deviceId = deviceResult.data;

  const otpResult = parseOTP(obj.otp);
  if (!otpResult.success) {
    return NextResponse.json({ error: 'Invalid OTP', details: otpResult.errors }, { status: 400 });
  }
  const otp = otpResult.data;

  const supabase = createClient(supabaseUrl, supabaseKey);

  const { data: device, error } = await supabase
    .from('devices')
    .select('*')
    .eq('mac_address', deviceId)
    .single();

  if (error || !device) {
    return NextResponse.json({ error: 'Device not found' }, { status: 404 });
  }

  if (!device.pair_code || !device.pair_expires_at) {
    return NextResponse.json(
      { error: 'No OTP requested for this device. Request OTP first.' },
      { status: 400 }
    );
  }

  if (new Date(device.pair_expires_at) < new Date()) {
    return NextResponse.json(
      { error: 'Mã OTP đã hết hạn. Vui lòng yêu cầu OTP mới.' },
      { status: 400 }
    );
  }

  if (!timingSafeEqual(device.pair_code, otp)) {
    await new Promise((r) => setTimeout(r, 100 + Math.random() * 100));
    return NextResponse.json({ error: 'Mã OTP không hợp lệ' }, { status: 400 });
  }

  const { error: updateError } = await supabase
    .from('devices')
    .update({
      is_paired: true,
      pair_code: null,
      pair_expires_at: null,
      last_seen_at: new Date().toISOString(),
      is_online: true,
    })
    .eq('id', device.id);

  if (updateError) {
    console.error('[verify-otp] Update error:', updateError);
    return NextResponse.json({ error: 'Failed to update device' }, { status: 500 });
  }

  try {
    await publishLoginSuccess(deviceId, device.tenant_id);
  } catch (mqttErr) {
    console.warn('[verify-otp] MQTT notification failed:', mqttErr);
  }

  // Send Telegram Notification if configured
  try {
    const telegramConfig = device.telegram_config as Record<string, any> | null;
    if (telegramConfig && telegramConfig.chat_id) {
      const message = `🟢 *Device Login Success*\n\n` +
                      `*Device Name:* ${escapeMarkdown(device.device_name || 'Unknown')}\n` +
                      `*Device ID:* \`${escapeMarkdown(deviceId)}\`\n` +
                      `*Time:* ${escapeMarkdown(new Date().toLocaleString('vi-VN'))}`;
      
      await sendTelegramMessage(telegramConfig.chat_id, message, {
        botToken: telegramConfig.bot_token,
      });
    }
  } catch (tgErr) {
    console.warn('[verify-otp] Telegram notification failed:', tgErr);
  }

  return NextResponse.json(
    {
      success: true,
      message: 'Xác thực thành công',
      device: {
        id: device.id,
        name: device.device_name,
        device_id: device.mac_address,
        tenant_id: device.tenant_id,
      },
    },
    { headers: { 'X-RateLimit-Remaining': String(rl.remaining) } }
  );
}

function timingSafeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let result = 0;
  for (let i = 0; i < a.length; i++) {
    result |= a.charCodeAt(i) ^ b.charCodeAt(i);
  }
  return result === 0;
}
