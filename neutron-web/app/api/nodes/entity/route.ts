import { createClient } from '@supabase/supabase-js';
import { NextRequest, NextResponse } from 'next/server';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

// POST /api/nodes/entity
// Create or find an entity node (person, topic, project)
// Body: { tenant_id, name, type: 'person'|'topic'|'project'|'event', content? }
export async function POST(request: NextRequest) {
  const body = await request.json().catch(() => null);
  const { tenant_id, name, subtype, content } = body ?? {};

  if (!tenant_id || !name) {
    return NextResponse.json({ error: 'tenant_id and name required' }, { status: 400 });
  }

  const supabase = createClient(supabaseUrl, supabaseKey);

  // Check if entity already exists
  const { data: existing } = await supabase
    .from('nodes')
    .select('id, name, type, subtype, content')
    .eq('tenant_id', tenant_id)
    .eq('name', name)
    .eq('type', 'entity')
    .limit(1)
    .single();

  if (existing) {
    // Update content merge
    const { data, error } = await supabase
      .from('nodes')
      .update({ content: existing.content || {}, updated_at: new Date().toISOString() })
      .eq('id', existing.id)
      .select()
      .single();

    if (error) return NextResponse.json({ error: error.message }, { status: 500 });
    return NextResponse.json({ node: data, created: false });
  }

  // Create new entity node
  const { data, error } = await supabase
    .from('nodes')
    .insert({
      tenant_id,
      name,
      type: 'entity',
      subtype: subtype ?? 'general',
      content: content ?? { created_at: new Date().toISOString() },
      is_active: true
    })
    .select()
    .single();

  if (error) return NextResponse.json({ error: error.message }, { status: 500 });

  // Create pulse: entity_mentioned
  await supabase.from('pulses').insert({
    tenant_id,
    source_node_id: null,
    target_node_id: data.id,
    type: 'perception',
    text: `Entity mentioned: ${name}`,
    metadata: { entity_name: name, subtype }
  }).catch(() => {});

  return NextResponse.json({ node: data, created: true }, { status: 201 });
}

// GET /api/nodes/entity?tenant_id=xxx&name=yyy
// Find entity by name (fuzzy)
export async function GET(request: NextRequest) {
  const tenantId = request.nextUrl.searchParams.get('tenant_id');
  const name = request.nextUrl.searchParams.get('name');
  const limit = parseInt(request.nextUrl.searchParams.get('limit') ?? '20');

  if (!tenantId) return NextResponse.json({ error: 'tenant_id required' }, { status: 400 });

  const supabase = createClient(supabaseUrl, supabaseKey);
  let query = supabase
    .from('nodes')
    .select('id, name, type, subtype, content, updated_at')
    .eq('tenant_id', tenantId)
    .eq('type', 'entity')
    .eq('is_active', true)
    .limit(limit);

  if (name) {
    query = query.ilike('name', `%${name}%`);
  }

  const { data, error } = await query;
  if (error) return NextResponse.json({ error: error.message }, { status: 500 });

  return NextResponse.json({ entities: data ?? [] });
}
