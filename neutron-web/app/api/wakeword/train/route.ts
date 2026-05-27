import { NextRequest, NextResponse } from 'next/server';
import { processWakewordApply } from '@/lib/wakeword-service';
import { parseWakewordApply } from '@/lib/validation';

/**
 * POST /api/wakeword/train
 * Wrapper: mode=train → /api/wakeword/apply
 */
export async function POST(request: NextRequest) {
  try {
    const body = await request.json();
    const wrapped = { ...body, mode: 'train' as const };
    const parsed = parseWakewordApply(wrapped);
    if (!parsed.success) {
      return NextResponse.json(
        { error: 'Validation failed', details: parsed.errors },
        { status: 400 }
      );
    }

    const d = parsed.data;
    const result = await processWakewordApply({
      mode: 'train',
      deviceId: d.deviceId,
      enabled: body.enabled !== false,
      label: d.label,
      samples: d.samples,
    });

    return NextResponse.json({
      success: true,
      mode: 'train',
      message: result.message,
      synced: result.synced,
      config: result.config,
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : 'Internal Server Error';
    console.error('[wakeword/train]', err);
    return NextResponse.json({ error: msg }, { status: 500 });
  }
}
