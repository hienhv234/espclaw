import { NextRequest, NextResponse } from 'next/server';
import { getMQTTPool } from '@/lib/mqtt-pool';

export async function POST(req: NextRequest) {
  try {
    const { deviceId, command } = await req.json();

    if (!deviceId || !command) {
      return NextResponse.json({ error: 'Missing deviceId or command' }, { status: 400 });
    }

    // Publish sync command to MQTT
    // Topic format: espclaw/{deviceId}/cmd
    const topic = `espclaw/${deviceId}/cmd`;
    const payload = JSON.stringify({ type: 'sys', action: command });

    const pool = getMQTTPool();
    await pool.publish(topic, payload);

    return NextResponse.json({ success: true });
  } catch (err) {
    console.error('Control API error:', err);
    return NextResponse.json({ error: 'Internal Server Error' }, { status: 500 });
  }
}
