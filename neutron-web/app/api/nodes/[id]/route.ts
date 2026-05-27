import { createClient } from '@supabase/supabase-js';
import { NextRequest, NextResponse } from 'next/server';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

// PATCH /api/nodes/[id]
// Update a node's content or properties
export async function PATCH(
  request: NextRequest,
  { params }: { params: { id: string } }
) {
  const id = params.id;
  const body = await request.json().catch(() => null);

  if (!body) {
    return NextResponse.json({ error: 'Body required' }, { status: 400 });
  }

  const supabase = createClient(supabaseUrl, supabaseKey);

  // Upsert node (insert if missing, update if exists)
  const { data, error } = await supabase
    .from('nodes')
    .upsert({
      id: id,
      ...body,
      updated_at: new Date().toISOString()
    })
    .select()
    .single();

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }

  return NextResponse.json({ node: data });
}

// GET /api/nodes/[id]
export async function GET(
  request: NextRequest,
  { params }: { params: { id: string } }
) {
  const id = params.id;
  const supabase = createClient(supabaseUrl, supabaseKey);

  const { data, error } = await supabase
    .from('nodes')
    .select('*')
    .eq('id', id)
    .single();

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 404 });
  }

  return NextResponse.json({ node: data });
}
