/**
 * Workflow Executions History
 * GET /api/workflows/[id]/executions
 */

import { NextRequest, NextResponse } from 'next/server';
import { createClient } from '@supabase/supabase-js';

const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_URL!;
const supabaseKey = process.env.SUPABASE_SERVICE_ROLE_KEY!;

function getSupabase() {
  return createClient(supabaseUrl, supabaseKey, {
    auth: { autoRefreshToken: false, persistSession: false }
  });
}

export async function GET(
  request: NextRequest,
  { params }: { params: Promise<{ id: string }> }
) {
  const { id } = await params;
  const limit = parseInt(request.nextUrl.searchParams.get('limit') ?? '20');

  const supabase = getSupabase();

  const { data, error } = await supabase
    .from('workflow_executions')
    .select(`
      *,
      workflows:workflow_id (id, name, icon, category)
    `)
    .eq('workflow_id', id)
    .order('started_at', { ascending: false })
    .limit(limit);

  if (error) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }

  return NextResponse.json({ executions: data ?? [] });
}
