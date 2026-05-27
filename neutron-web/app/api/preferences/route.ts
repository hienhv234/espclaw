import { createClient } from '@supabase/supabase-js';
import { NextRequest, NextResponse } from 'next/server';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

const sb = () => createClient(supabaseUrl, supabaseKey);

// GET /api/preferences?tenant_id=xxx&user_id=yyy&key=zzz
export async function GET(request: NextRequest) {
  const tenantId = request.nextUrl.searchParams.get('tenant_id');
  const userId = request.nextUrl.searchParams.get('user_id') ?? null;
  const key = request.nextUrl.searchParams.get('key');

  if (!tenantId) return NextResponse.json({ error: 'tenant_id required' }, { status: 400 });

  const supabase = sb();

  if (key) {
    const { data, error } = await supabase.rpc('get_preference', {
      p_tenant_id: tenantId,
      p_user_id: userId,
      p_key: key
    });
    if (error) return NextResponse.json({ error: error.message }, { status: 500 });
    return NextResponse.json({ key, value: data });
  }

  // Get all preferences for tenant
  let query = supabase.from('preferences').select('*').eq('tenant_id', tenantId);
  if (userId) query = query.eq('user_id', userId);
  const { data, error } = await query;
  if (error) return NextResponse.json({ error: error.message }, { status: 500 });
  return NextResponse.json({ preferences: data ?? [] });
}

// POST /api/preferences { tenant_id, user_id?, key, value }
export async function POST(request: NextRequest) {
  const body = await request.json().catch(() => null);
  const { tenant_id, user_id, key, value } = body ?? {};

  if (!tenant_id || !key || value === undefined) {
    return NextResponse.json({ error: 'tenant_id, key, value required' }, { status: 400 });
  }

  const supabase = sb();
  const result = await supabase.rpc('set_preference', {
    p_tenant_id: tenant_id,
    p_user_id: user_id ?? null,
    p_key: key,
    p_value: typeof value === 'string' ? JSON.parse(value) : value
  });

  if (result.error) return NextResponse.json({ error: result.error.message }, { status: 500 });
  return NextResponse.json(result.data, { status: 201 });
}
