import { NextRequest, NextResponse } from 'next/server';
import { getMQTTPool } from '@/lib/mqtt-pool';

/**
 * POST /api/device/config-pull
 * Trigger selective config pull on ESP device via MQTT.
 *
 * Sends "pull_config" command to the device — device will only
 * apply cloud config if local NVS is empty (preserves existing config).
 *
 * Body: { deviceId: string }
 */
export async function POST(request: NextRequest) {
  try {
    const { deviceId } = await request.json();

    if (!deviceId) {
      return NextResponse.json({ error: 'deviceId is required' }, { status: 400 });
    }

    const topic = `espclaw/${deviceId}/cmd`;
    const payload = JSON.stringify({
      type: 'sys',
      action: 'pull_config',
      ts: Date.now(),
    });

    const pool = getMQTTPool();
    await pool.publish(topic, payload, 1);

    return NextResponse.json({
      success: true,
      deviceId,
      message: 'Config pull command sent to device. ESP will apply cloud config only if local NVS is empty.',
    });
  } catch (err) {
    console.error('[config-pull] Error:', err);
    return NextResponse.json({ error: 'Internal Server Error' }, { status: 500 });
  }
}
