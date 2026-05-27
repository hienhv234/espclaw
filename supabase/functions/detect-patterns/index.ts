// Supabase Edge Function - Pattern Detection
// Detects co-occurrence patterns in pulse data

import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
};

interface Pattern {
  pattern_id: string;
  node_type_a: string;
  node_type_b: string;
  cooccurrence_count: number;
  confidence: number;
  avg_time_gap_seconds: number;
  suggestion: string;
}

serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  try {
    const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
    const supabaseKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
    const supabase = createClient(supabaseUrl, supabaseKey);

    const { tenant_id, lookback_hours = 24 } = await req.json();

    if (!tenant_id) {
      return new Response(JSON.stringify({ error: "tenant_id required" }), {
        status: 400,
        headers: { ...corsHeaders, "Content-Type": "application/json" },
      });
    }

    const lookbackTime = new Date(Date.now() - lookback_hours * 60 * 60 * 1000).toISOString();

    // Get recent pulses
    const { data: pulses, error: pulsesError } = await supabase
      .from("pulses")
      .select("*")
      .eq("tenant_id", tenant_id)
      .gte("created_at", lookbackTime)
      .order("created_at", { ascending: true });

    if (pulsesError) throw pulsesError;

    const patterns: Pattern[] = [];
    const nodePairs = new Map<string, { count: number; timeGaps: number[] }>();

    // Analyze pulse sequences
    for (let i = 1; i < pulses.length; i++) {
      const prev = pulses[i - 1];
      const curr = pulses[i];

      if (prev.source_node_id && curr.source_node_id) {
        const pairKey = `${prev.source_node_id}:${curr.source_node_id}`;
        const reverseKey = `${curr.source_node_id}:${prev.source_node_id}`;

        // Check both directions
        const existing = nodePairs.get(pairKey) || nodePairs.get(reverseKey);
        const timeGap = new Date(curr.created_at).getTime() - new Date(prev.created_at).getTime();

        if (existing) {
          existing.count++;
          existing.timeGaps.push(timeGap);
        } else {
          nodePairs.set(pairKey, { count: 1, timeGaps: [timeGap] });
        }
      }
    }

    // Get node info for patterns with high confidence
    for (const [pairKey, data] of nodePairs) {
      if (data.count < 3) continue; // Minimum occurrences

      const [nodeA, nodeB] = pairKey.split(":");
      const avgGap = data.timeGaps.reduce((a, b) => a + b, 0) / data.timeGaps.length;
      const confidence = Math.min(data.count / 10, 1.0); // Normalize confidence

      if (confidence >= 0.3) {
        // Get node types
        const { data: nodes } = await supabase
          .from("nodes")
          .select("id, type, name")
          .in("id", [nodeA, nodeB]);

        const nodeMap = new Map(nodes?.map((n) => [n.id, n]) || []);

        patterns.push({
          pattern_id: crypto.randomUUID(),
          node_type_a: nodeMap.get(nodeA)?.type || "unknown",
          node_type_b: nodeMap.get(nodeB)?.type || "unknown",
          cooccurrence_count: data.count,
          confidence,
          avg_time_gap_seconds: Math.round(avgGap / 1000),
          suggestion: generateSuggestion(nodeMap.get(nodeA), nodeMap.get(nodeB), avgGap),
        });
      }
    }

    // Sort by confidence
    patterns.sort((a, b) => b.confidence - a.confidence);

    // Store patterns in database
    for (const pattern of patterns.slice(0, 10)) {
      await supabase.from("patterns").upsert({
        tenant_id,
        pattern_id: pattern.pattern_id,
        node_type_a: pattern.node_type_a,
        node_type_b: pattern.node_type_b,
        cooccurrence_count: pattern.cooccurrence_count,
        confidence: pattern.confidence,
        avg_time_gap_seconds: pattern.avg_time_gap_seconds,
        suggestion: pattern.suggestion,
        detected_at: new Date().toISOString(),
      });
    }

    return new Response(
      JSON.stringify({
        patterns,
        summary: {
          total_pulses_analyzed: pulses.length,
          patterns_found: patterns.length,
          time_range_hours: lookback_hours,
        },
      }),
      { headers: { ...corsHeaders, "Content-Type": "application/json" } }
    );
  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 500,
      headers: { ...corsHeaders, "Content-Type": "application/json" },
    });
  }
});

function generateSuggestion(nodeA: any, nodeB: any, avgGap: number): string {
  if (!nodeA || !nodeB) return "Consider creating a link between these nodes";

  const gapMinutes = Math.round(avgGap / 60000);

  if (nodeA.type === "event" && nodeB.type === "event") {
    return `Events of type "${nodeA.name}" often precede "${nodeB.name}" by ~${gapMinutes} minutes. Consider creating a "followed_by" link.`;
  }

  if (nodeA.type === "entity" || nodeB.type === "entity") {
    return `Consider linking "${nodeA.name}" to "${nodeB.name}" as they appear together frequently.`;
  }

  return `High co-occurrence detected. Consider creating a semantic link between these concepts.`;
}
