// Supabase Edge Function - AI Insights
// Uses Claude API to analyze graph and generate insights

import { serve } from "https://deno.land/std@0.168.0/http/server.ts";
import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
};

const ANTHROPIC_API_URL = "https://api.anthropic.com/v1/messages";
const MODEL = "claude-sonnet-4-20250514"; // Stable Sonnet model — not claude-haiku which is invalid

serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  try {
    const supabaseUrl = Deno.env.get("SUPABASE_URL")!;
    const supabaseKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
    const anthropicApiKey = Deno.env.get("ANTHROPIC_API_KEY");

    if (!anthropicApiKey) {
      return new Response(
        JSON.stringify({ error: "ANTHROPIC_API_KEY not configured" }),
        { status: 500, headers: { ...corsHeaders, "Content-Type": "application/json" } }
      );
    }

    const supabase = createClient(supabaseUrl, supabaseKey);

    const { tenant_id, analysis_type = "full" } = await req.json();

    if (!tenant_id) {
      return new Response(JSON.stringify({ error: "tenant_id required" }), {
        status: 400,
        headers: { ...corsHeaders, "Content-Type": "application/json" },
      });
    }

    // Fetch graph data
    const [nodesResult, linksResult, pulsesResult] = await Promise.all([
      supabase.from("nodes").select("*").eq("tenant_id", tenant_id).limit(100),
      supabase.from("links").select("*").eq("tenant_id", tenant_id).limit(100),
      supabase
        .from("pulses")
        .select("*")
        .eq("tenant_id", tenant_id)
        .gte("created_at", new Date(Date.now() - 7 * 24 * 60 * 60 * 1000).toISOString())
        .limit(50),
    ]);

    const nodes = nodesResult.data || [];
    const links = linksResult.data || [];
    const pulses = pulsesResult.data || [];

    // Build prompt for Claude
    const prompt = buildAnalysisPrompt(nodes, links, pulses, analysis_type);

    // Call Claude API
    const response = await fetch(ANTHROPIC_API_URL, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "x-api-key": anthropicApiKey,
        "anthropic-version": "2023-06-01",
      },
      body: JSON.stringify({
        model: MODEL,
        max_tokens: 1024,
        messages: [{ role: "user", content: prompt }],
      }),
    });

    if (!response.ok) {
      const errorText = await response.text();
      throw new Error(`Anthropic API error: ${response.status} - ${errorText}`);
    }

    const result = await response.json();
    const insight = result.content?.[0]?.text || "No insight generated";

    // Store insight
    const insightRecord = {
      tenant_id,
      insight_text: insight,
      analysis_type,
      node_count: nodes.length,
      link_count: links.length,
      pulse_count: pulses.length,
    };

    await supabase.from("ai_insights").insert(insightRecord);

    return new Response(
      JSON.stringify({
        insight,
        metadata: {
          nodes_analyzed: nodes.length,
          links_analyzed: links.length,
          pulses_analyzed: pulses.length,
          analysis_type,
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

function buildAnalysisPrompt(
  nodes: any[],
  links: any[],
  pulses: any[],
  analysisType: string
): string {
  const nodeSummary = nodes
    .map((n) => `- ${n.type}: ${n.name} (${n.subtype || "none"})`)
    .join("\n");

  const linkSummary = links
    .slice(0, 20)
    .map((l) => `- ${l.type} link`)
    .join("\n");

  const nodeTypes = countByType(nodes);
  const linkTypes = countByType(links.map((l) => ({ type: l.type })));

  return `You are analyzing a knowledge graph for a cognitive AI system called ESPClaw.

GRAPH STATISTICS:
- Total nodes: ${nodes.length}
- Total links: ${links.length}
- Total pulses (7 days): ${pulses.length}

NODE TYPES:
${Object.entries(nodeTypes)
  .map(([type, count]) => `- ${type}: ${count}`)
  .join("\n")}

LINK TYPES:
${Object.entries(linkTypes)
  .map(([type, count]) => `- ${type}: ${count}`)
  .join("\n")}

SAMPLE NODES:
${nodeSummary}

SAMPLE LINKS:
${linkSummary}

${analysisType === "summary" ? `
Provide a brief summary (2-3 sentences) of this knowledge graph.
` : analysisType === "suggestions" ? `
Based on the graph structure, suggest 3-5 specific actions to improve the knowledge graph.
Focus on:
1. Missing connections between related concepts
2. Orphan nodes that should be connected
3. Patterns that suggest new skills or routines
` : `
Provide a comprehensive analysis of this knowledge graph including:
1. Key themes and patterns
2. Missing connections
3. Recommendations for improvement
4. Predictions for what the user might want next
`}`;
}

function countByType(items: any[]): Record<string, number> {
  return items.reduce((acc, item) => {
    const type = item.type || "unknown";
    acc[type] = (acc[type] || 0) + 1;
    return acc;
  }, {} as Record<string, number>);
}
