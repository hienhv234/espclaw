import { NextResponse } from 'next/server';
import { getMQTTPool } from '@/lib/mqtt-pool';

export async function GET() {
  const pool = getMQTTPool();
  try {
    await pool.ensureConnected();
    const connected = pool.isConnected();
    return NextResponse.json({
      mqtt_connected: connected,
      timestamp: new Date().toISOString(),
    });
  } catch (err: any) {
    return NextResponse.json({
      mqtt_connected: false,
      error: err.message,
      timestamp: new Date().toISOString(),
    }, { status: 500 });
  }
}
