/**
 * app/api/wakeword/mic-test/route.ts
 *
 * POST: gui lenh mic test den ESP, collect PCM chunks, tra ve WAV.
 * GET:  serve file WAV tu /tmp/.
 *
 * Debug endpoints:
 *   POST /api/wakeword/mic-test?debug=1  — tra ve chi tiet log
 *   GET  /api/wakeword/mic-test/diag     — test MQTT connectivity voi ESP
 */
import { NextRequest, NextResponse } from 'next/server';
import { getMQTTPool } from '@/lib/mqtt-pool';
import { parseDeviceId } from '@/lib/validation';
import { writeFileSync, unlinkSync, existsSync, mkdirSync, readdirSync, statSync } from 'fs';
import { join } from 'path';
import { mkdtempSync } from 'fs';
import { tmpdir } from 'os';

/* ── WAV constants ─────────────────────────────────────────────────────── */
const WAV_HDR_BYTES = 44;
const SAMPLE_RATE  = 16000;
const CHANNELS     = 1;
const BPS          = 16;
const BLOCK_ALIGN  = (CHANNELS * BPS) / 8;
const BYTE_RATE    = SAMPLE_RATE * BLOCK_ALIGN;

function buildWavHeader(dataBytes: number): Buffer {
  const buf = Buffer.alloc(WAV_HDR_BYTES);
  buf.write('RIFF', 0);
  buf.writeUInt32LE(WAV_HDR_BYTES - 8 + dataBytes, 4);
  buf.write('WAVE', 8);
  buf.write('fmt ', 12);
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20);
  buf.writeUInt16LE(CHANNELS, 22);
  buf.writeUInt32LE(SAMPLE_RATE, 24);
  buf.writeUInt32LE(BYTE_RATE, 28);
  buf.writeUInt16LE(BLOCK_ALIGN, 32);
  buf.writeUInt16LE(BPS, 34);
  buf.write('data', 36);
  buf.writeUInt32LE(dataBytes, 40);
  return buf;
}

/* ── Temp file management ─────────────────────────────────────────────── */
const tmpDir = '/tmp/espclaw-audio';
try {
  if (!existsSync(tmpDir)) {
    mkdirSync(tmpDir, { recursive: true });
  }
} catch (e) {
  console.error('[mic-test] Failed to initialize tmpDir:', e);
}
const pendingFiles = new Map<string, NodeJS.Timeout>();

setInterval(() => {
  try {
    const files = readdirSync(tmpDir);
    const now = Date.now();
    for (const f of files) {
      const fp = join(tmpDir, f);
      try {
        if (now - statSync(fp).mtimeMs > 120_000) {
          unlinkSync(fp);
          console.log(`[mic-test] Cleaned: ${f}`);
        }
      } catch { /* already deleted */ }
    }
  } catch { /* dir gone */ }
}, 300_000);

function scheduleCleanup(path: string, delayMs = 60_000) {
  const existing = pendingFiles.get(path);
  if (existing) clearTimeout(existing);
  const t = setTimeout(() => {
    try { unlinkSync(path); } catch { /* gone */ }
    pendingFiles.delete(path);
  }, delayMs);
  pendingFiles.set(path, t);
}

/* ── Debug: echo test — gui MQTT, doi ESP phan hoi tren topic rieng ─── */
async function mqttEchoTest(deviceId: string, timeoutMs = 5000): Promise<{
  cmdSent: boolean;
  echoReceived: boolean;
  echoPayload: string;
  audioChunksReceived: number;
}> {
  const pool = getMQTTPool();
  const echoTopic = `espclaw/${deviceId}/echo`;
  const cmdTopic = `espclaw/${deviceId}/cmd`;
  const audioTopic = `espclaw/${deviceId}/audio`;

  let echoReceived = false;
  let echoPayload = '';
  let audioChunksReceived = 0;

  return new Promise((resolve) => {
    const deadline = Date.now() + timeoutMs;

    // Subscribe audio topic (for chunk count)
    pool.subscribeRaw(audioTopic, (_, payload) => {
      audioChunksReceived++;
    }, 1);

    // Subscribe echo topic
    pool.subscribeRaw(echoTopic, (_, payload) => {
      echoReceived = true;
      echoPayload = payload.toString('utf8');
    }, 1);

    // Gui echo test command — ESP se phan hoi tren /echo
    pool.publish(cmdTopic, JSON.stringify({
      type: 'diag',
      action: 'echo',
      echo_topic: echoTopic,
      ts: Date.now(),
    }), 1).then(() => {
      // Poll cho den khi nhan duoc echo hoac timeout
      const poll = () => {
        if (echoReceived || Date.now() >= deadline) {
          resolve({
            cmdSent: true,
            echoReceived,
            echoPayload,
            audioChunksReceived,
          });
        } else {
          setTimeout(poll, 100);
        }
      };
      setTimeout(poll, 100);
    }).catch(() => {
      resolve({ cmdSent: false, echoReceived, echoPayload, audioChunksReceived });
    });
  });
}

/* ── POST: Mic test ────────────────────────────────────────────────────── */
export async function POST(request: NextRequest) {
  try {
    const url = new URL(request.url);
    const isDebug = url.searchParams.get('debug') === '1';

    const body = await request.json().catch(() => ({}));
    const deviceIdRaw = body?.deviceId ?? body?.device_id ?? '';
    const durationMs = Math.max(500, Math.min(10000, Number(body?.durationMs ?? 1800)));

    const parsed = parseDeviceId(deviceIdRaw);
    if (!parsed.success) {
      return NextResponse.json({ error: 'deviceId khong hop le' }, { status: 400 });
    }
    const deviceId = parsed.data;

    const audioTopic = `espclaw/${deviceId}/audio`;
    const cmdTopic  = `espclaw/${deviceId}/cmd`;
    const pool = getMQTTPool();

    if (isDebug) {
      console.log(`[mic-test] DEBUG: device=${deviceId} duration=${durationMs}ms`);
      console.log(`[mic-test] DEBUG: pool connected=${pool.isConnected()}`);
    }

    /* ── Buoc 0: MQTT echo test (neu debug) ── */
    if (isDebug) {
      const diag = await mqttEchoTest(deviceId, 5000);
      console.log(`[mic-test] Echo: cmdSent=${diag.cmdSent} echoReceived=${diag.echoReceived} audioChunks=${diag.audioChunksReceived}`);
      if (!diag.cmdSent) {
        return NextResponse.json({ error: 'MQTT khong gui duoc command', diag }, { status: 502 });
      }
      if (!diag.echoReceived) {
        return NextResponse.json({
          error: 'ESP khong phan hoi — firmware chua co mic_test.c hoac MQTT loi.',
          diag,
        }, { status: 504 });
      }
      return NextResponse.json({ success: true, diag });
    }

    /* ── Buoc 1: Subscribe + gui command ── */
    const chunks: Buffer[] = [];
    let done = false;
    let doneReason = '';
    let receivedChunks = 0;

    await new Promise<void>((resolveSubscribe, rejectSubscribe) => {
      pool.subscribeRaw(audioTopic, (_, payload) => {
        const text = payload.toString('utf8', 0, Math.min(128, payload.length));
        if (text.startsWith('{"type":"mic_test_done"') ||
            text.startsWith('{"type":"diag_echo"')) {
          done = true;
          doneReason = text;
          return;
        }
        receivedChunks++;
        chunks.push(Buffer.from(payload));
      }, 1);

      const cmdPayload = JSON.stringify({
        type: 'mic_test',
        action: 'start',
        duration_ms: durationMs,
        ts: Date.now(),
      });

      pool.publish(cmdTopic, cmdPayload, 1).then(() => resolveSubscribe())
        .catch((err: Error) => rejectSubscribe(err));
    });

    /* ── Buoc 2: Cho ket qua hoac timeout ── */
    const TIMEOUT_MS = durationMs + 8000;
    const deadline = Date.now() + TIMEOUT_MS;
    while (!done && Date.now() < deadline) {
      await new Promise(r => setTimeout(r, 50));
    }

    if (!done || chunks.length === 0) {
      return NextResponse.json({
        error: `ESP khong phan hoi. chunks=${receivedChunks} done=${done}`,
        chunks: receivedChunks,
        done,
        doneReason,
      }, { status: 504 });
    }

    /* ── Buoc 3: Build WAV ── */
    const totalPcmBytes = chunks.reduce((s, c) => s + c.length, 0);
    const wavBuf = Buffer.alloc(WAV_HDR_BYTES + totalPcmBytes);
    buildWavHeader(totalPcmBytes).copy(wavBuf);
    let offset = WAV_HDR_BYTES;
    for (const c of chunks) { c.copy(wavBuf, offset); offset += c.length; }

    console.log(`[mic-test] WAV built: ${wavBuf.length} bytes, ${chunks.length} chunks`);

    /* Save WAV to tmpDir so it can be used for Edge Impulse training */
    const fileName = `${deviceId}-${Date.now()}.wav`;
    const filePath = join(tmpDir, fileName);
    try {
      writeFileSync(filePath, wavBuf);
      scheduleCleanup(filePath, 300_000); // 5 minutes cleanup
      console.log(`[mic-test] Saved sample to disk: ${filePath}`);
    } catch (e) {
      console.error('[mic-test] Failed to write WAV sample to disk:', e);
    }

    /* ── Tra ve WAV truc tiep (Blob) — khong redirect ── */
    return new NextResponse(wavBuf, {
      headers: {
        'Content-Type': 'audio/wav',
        'Content-Disposition': `inline; filename="${fileName}"`,
        'Content-Length': String(wavBuf.length),
        'X-Chunk-Count': String(chunks.length),
        'X-Duration-Ms': String(durationMs),
        'X-File-Name': fileName,
        'Cache-Control': 'no-store',
      },
    });

  } catch (err) {
    console.error('[mic-test POST]', err);
    return NextResponse.json({ error: 'Loi noi bo' }, { status: 500 });
  }
}

/* ── GET: Serve WAV file ───────────────────────────────────────────────── */
export async function GET(request: NextRequest) {
  try {
    const { searchParams } = new URL(request.url);
    const fileName = searchParams.get('file');

    if (!fileName || fileName.includes('..') || !fileName.endsWith('.wav')) {
      return NextResponse.json({ error: 'Ten file khong hop le' }, { status: 400 });
    }

    const filePath = join(tmpDir, fileName);
    if (!existsSync(filePath)) {
      return NextResponse.json({ error: 'File khong ton tai hoac da bi xoa' }, { status: 404 });
    }

    const fileBuffer = readFileSync(filePath);
    scheduleCleanup(filePath, 10_000);

    return new NextResponse(fileBuffer, {
      headers: {
        'Content-Type': 'audio/wav',
        'Content-Disposition': `inline; filename="${fileName}"`,
        'Content-Length': String(fileBuffer.length),
        'Cache-Control': 'no-store',
      },
    });

  } catch (err) {
    console.error('[mic-test GET]', err);
    return NextResponse.json({ error: 'Loi doc file' }, { status: 500 });
  }
}
