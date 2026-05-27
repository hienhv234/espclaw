import { NextRequest, NextResponse } from 'next/server';

/**
 * @deprecated Dùng POST /api/wakeword/apply với mode=library|keyword
 */
export async function POST(request: NextRequest) {
  return NextResponse.json(
    {
      error: 'API đã đổi. Dùng POST /api/wakeword/apply với mode: library | keyword | train',
    },
    { status: 410 }
  );
}
