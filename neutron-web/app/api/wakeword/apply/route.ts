import { NextRequest, NextResponse } from 'next/server';
import { processWakewordApply } from '@/lib/wakeword-service';
import { parseWakewordApply } from '@/lib/validation';

/**
 * POST /api/wakeword/apply
 *
 * Một endpoint — 3 mode:
 * - library: thư viện có sẵn → sync ESP
 * - keyword: nhập từ khóa → backend xử lý → sync ESP
 * - train: thu mẫu dương/âm → backend xử lý → sync ESP
 */
export async function POST(request: NextRequest) {
  try {
    const body = await request.json();
    const parsed = parseWakewordApply(body);
    if (!parsed.success) {
      return NextResponse.json(
        { error: 'Validation failed', details: parsed.errors },
        { status: 400 }
      );
    }

    const d = parsed.data;
    const result = await processWakewordApply({
      mode: d.mode,
      deviceId: d.deviceId,
      enabled: d.enabled,
      presetId: d.presetId,
      keyword: d.keyword,
      label: d.label,
      samples: d.samples,
    });

    return NextResponse.json({
      success: true,
      mode: d.mode,
      message: result.message,
      synced: result.synced,
      config: result.config,
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : 'Internal Server Error';
    console.error('[wakeword/apply]', err);
    return NextResponse.json({ error: msg }, { status: msg.includes('bắt buộc') || msg.includes('Preset') ? 400 : 500 });
  }
}
