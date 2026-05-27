import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

export async function POST(request: NextRequest) {
  try {
    const { device_id, code } = await request.json();

    if (!device_id || !code) {
      return NextResponse.json({ error: 'Missing device_id or code' }, { status: 400 });
    }

    const supabase = createClient(supabaseUrl, supabaseKey);

    // Verify pairing code
    const { data: device, error } = await supabase
      .from('devices')
      .select('*')
      .eq('id', device_id)
      .eq('pairing_code', code)
      .gte('pairing_expires_at', new Date().toISOString())
      .single();

    if (error || !device) {
      return NextResponse.json({ error: 'Invalid or expired code' }, { status: 400 });
    }

    // Update device status - clear pairing code, set online
    await supabase
      .from('devices')
      .update({
        pairing_code: null,
        pairing_expires_at: null,
        is_online: true,
        last_seen_at: new Date().toISOString()
      })
      .eq('id', device_id);

    // TODO: Publish MQTT message to device confirming pairing
    // For now, device will poll or use realtime subscription
    // await mqttClient.publish(`espclaw/${device_id}/config`, JSON.stringify({ paired: true }));

    return NextResponse.json({
      success: true,
      message: 'Device paired successfully',
      device: {
        id: device.id,
        name: device.name
      }
    });

  } catch (err: any) {
    return NextResponse.json({ error: err.message }, { status: 500 });
  }
}
