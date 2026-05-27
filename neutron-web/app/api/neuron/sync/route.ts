import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';
import { parseSyncDelta } from '@/lib/validation';
import { rateLimit } from '@/lib/rate-limit';
import crypto from 'crypto';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

// Helper to deterministically convert string-prefixed IDs to valid UUIDs
function toUuid(idStr: string | undefined): string | undefined {
  if (!idStr) return idStr;
  const UUID_REGEX = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  if (UUID_REGEX.test(idStr)) return idStr;
  // Generate deterministic UUID v4 format from string hash
  const hash = crypto.createHash('md5').update(idStr).digest('hex');
  return `${hash.slice(0, 8)}-${hash.slice(8, 12)}-4${hash.slice(13, 16)}-a${hash.slice(17, 20)}-${hash.slice(20, 32)}`;
}

export async function POST(request: NextRequest) {
  const rl = await rateLimit(request, 'neuron-api');
  if (!rl.success) {
    return NextResponse.json({ error: 'Rate limit exceeded' }, { status: 429 });
  }

  let body: unknown;
  try {
    body = await request.json();
  } catch {
    return NextResponse.json({ error: 'Invalid JSON body' }, { status: 400 });
  }

  const parsed = parseSyncDelta(body);
  if (!parsed.success) {
    return NextResponse.json(
      { error: 'Validation failed', details: parsed.errors },
      { status: 400 }
    );
  }

  const { tenant_id, device_id, nodes, links, pulses, last_sync_timestamp } = parsed.data;
  const supabase = createClient(supabaseUrl, supabaseKey);

  const results = {
    nodes_upserted: 0,
    nodes_deleted: 0,
    links_upserted: 0,
    links_deleted: 0,
    pulses_inserted: 0,
  };

  if (nodes && nodes.length > 0) {
    const toUpsert = nodes.filter((n: any) => !n.deleted_at);
    const toDelete = nodes.filter((n: any) => n.deleted_at);

    if (toUpsert.length > 0) {
      const { error: nodeError } = await supabase.from('nodes').upsert(
        toUpsert.map((n: any) => ({
          ...n,
          id: toUuid(n.id),
          tenant_id,
          device_id: device_id ?? null,
          is_active: true,
          is_synced: true,
        })),
        { onConflict: 'id', ignoreDuplicates: false }
      );
      if (!nodeError) {
        results.nodes_upserted = toUpsert.length;
        
        /* 
         * [AUTO-TEST] Telegram connectivity
         * If a Telegram interface node is being synced from the device, 
         * send a welcome message to verify the token/chat_id.
         */
        const tgNode = toUpsert.find((n: any) => n.subtype === 'interface_telegram');
        if (tgNode && tgNode.content?.bot_token && tgNode.content?.chat_id) {
          (async () => {
            try {
              const { sendTelegramMessage, escapeMarkdown } = await import('@/lib/telegram');
              await sendTelegramMessage(
                tgNode.content.chat_id,
                escapeMarkdown(`✅ *Đồng bộ thành công!* \nThiết bị [${device_id || 'ESP32'}] đã cập nhật cấu hình Telegram vào Graph. \nHệ thống đã sẵn sàng nhận lệnh!`),
                { botToken: tgNode.content.bot_token }
              );
              console.log(`[sync] Telegram welcome sent for tenant ${tenant_id}`);
            } catch (e) {
              console.error('[sync] Telegram welcome failed:', e);
            }
          })();
        }
      }
    }

    if (toDelete.length > 0) {
      const idsToDelete = toDelete.map((n: any) => toUuid(n.id)).filter(Boolean) as string[];
      if (idsToDelete.length > 0) {
        const { error } = await supabase
          .from('nodes')
          .update({ is_active: false, deleted_at: new Date().toISOString() })
          .in('id', idsToDelete);
        if (!error) results.nodes_deleted = idsToDelete.length;
      }
    }
  }

  if (links && links.length > 0) {
    const toUpsert = links.filter((l: any) => !l.deleted_at);
    const toDelete = links.filter((l: any) => l.deleted_at);

    if (toUpsert.length > 0) {
      const { error: linkError } = await supabase.from('links').upsert(
        toUpsert.map((l: any) => ({
          ...l,
          id: toUuid(l.id),
          source_id: toUuid(l.source_id),
          target_id: toUuid(l.target_id),
          tenant_id,
          device_id: device_id ?? null,
          is_active: true,
          is_synced: true,
        })),
        { onConflict: 'id', ignoreDuplicates: false }
      );
      if (!linkError) results.links_upserted = toUpsert.length;
    }

    if (toDelete.length > 0) {
      const idsToDelete = toDelete.map((l: any) => toUuid(l.id)).filter(Boolean) as string[];
      if (idsToDelete.length > 0) {
        const { error } = await supabase
          .from('links')
          .update({ is_active: false, deleted_at: new Date().toISOString() })
          .in('id', idsToDelete);
        if (!error) results.links_deleted = idsToDelete.length;
      }
    }
  }

  if (pulses && pulses.length > 0) {
    const { error: pulseError } = await supabase.from('pulses').insert(
      pulses.map((p: any) => ({
        ...p,
        source_node_id: toUuid(p.source_node_id) ?? null,
        target_node_id: toUuid(p.target_node_id) ?? null,
        link_id: toUuid(p.link_id) ?? null,
        tenant_id,
        device_id: device_id ?? null,
      }))
    );
    if (!pulseError) results.pulses_inserted = pulses.length;
  }

  const sinceTimestamp = last_sync_timestamp
    ? new Date(last_sync_timestamp)
    : new Date(0);

  const [nodesResult, linksResult, pulsesResult] = await Promise.all([
    last_sync_timestamp
      ? supabase.from('nodes').select('*').eq('tenant_id', tenant_id).eq('is_active', true).gte('updated_at', sinceTimestamp.toISOString()).limit(100)
      : Promise.resolve({ data: [] }),
    last_sync_timestamp
      ? supabase.from('links').select('*').eq('tenant_id', tenant_id).eq('is_active', true).gte('updated_at', sinceTimestamp.toISOString()).limit(200)
      : Promise.resolve({ data: [] }),
    last_sync_timestamp
      ? supabase.from('pulses').select('*').eq('tenant_id', tenant_id).gte('created_at', sinceTimestamp.toISOString()).limit(500)
      : Promise.resolve({ data: [] }),
  ]);

  const syncTimestamp = Date.now();

  return NextResponse.json(
    {
      success: true,
      sync_timestamp: syncTimestamp,
      incoming: results,
      changes: {
        nodes: nodesResult.data ?? [],
        links: linksResult.data ?? [],
        pulses: pulsesResult.data ?? [],
      },
      counts: {
        nodes: (nodesResult.data ?? []).length,
        links: (linksResult.data ?? []).length,
        pulses: (pulsesResult.data ?? []).length,
      },
    },
    { headers: { 'X-RateLimit-Remaining': String(rl.remaining) } }
  );
}
